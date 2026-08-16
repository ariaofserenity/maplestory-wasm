# Skill model, as implemented by the GMS v95 client

Everything below was recovered from `bin/GMSv95.exe` with its matching
`bin/MapleStory.pdb` (full private symbols, so struct layouts and member names
are the originals, not inferred). It supersedes the shape-sniffing heuristics
the WASM client previously used to decide what a skill's WZ nodes meant.

Where a statement is an inference rather than something read directly out of the
binary it is marked **(inferred)**.

## How to reproduce the findings

The client keeps almost every WZ property name in an obfuscated `StringPool`, so
a raw disassembly shows `StringPool::GetBSTR(pool, 0x1ab7)` instead of `"hit"`.
Two helper scripts under `scripts/re/` undo that:

- `scripts/re/strpool.py` decodes all 6883 pool entries out of the executable
  and writes `strpool.txt`. The cipher is: `ms_aString[i]` points at a seed byte
  followed by a NUL-terminated blob; the 16-byte key at `ms_aKey` is rotated
  *left by `(int8_t)seed` bits* and XOR'd over the blob, with a decoded `0`
  meaning "the plaintext byte equals the key byte".
- `scripts/re/annot.py <start_va> <stop_va>` disassembles a range and annotates
  every call, resolving pool indices to their plaintext and call targets to
  their PDB names.

Reference addresses (image base `0x400000`):

| Symbol | Address |
| --- | --- |
| `ms_aString` | `0x00C5A878` (6883 entries) |
| `ms_aKey` | `0x00B98830` (16 bytes) |
| `StringPool::GetString` | `0x00746750` |
| `StringPool::GetBSTR` | `0x00404BB0` |
| `CSkillInfo::LoadSkill` | `0x0070C190` |
| `SKILLLEVELDATA::LoadLevelData` | `0x00700990` |
| `CHARLEVELDATA::LoadCharLevelData` | `0x006FD040` |
| `SKILLENTRY::GetLevelData` | `0x00708E10` |
| `SKILLENTRY::GetCharLevelData` | `0x00709110` |
| `SKILLENTRY::GetCharLevelIndex` | `0x006EF640` |
| `SKILLENTRY::GetEffectUOL` | `0x00437130` |
| `SKILLENTRY::GetHitUOLByIndex` | `0x00649F50` |
| `SKILLENTRY::GetBallUOL` | `0x00569390` |
| `SKILLENTRY::GetRandomAppointedAction` | `0x0070AD40` |
| `CSkill_HitAni::CSkill_HitAni` | `0x006ECA00` |
| `CSkill_HitAni::CreateDefault` | `0x006EC010` |
| `CUser::ShowSkillEffect` | `0x008F6F60` |

## Where skills come from

`CSkillInfo::IterateSkillInfo` walks the children of the `Skill` namespace. It
skips any child whose name starts with `MC`, `Ite` or `BF`, plus two names taken
from the string pool, and passes everything else to `LoadSkillRoot(id, prop)`,
which in turn calls `LoadSkill` per skill. So mob skills, item skills and the
`BF` books are loaded through separate paths and never become a `SKILLENTRY`.

## SKILLENTRY

`sizeof == 612`. Members, in declaration order, with the WZ property each is
loaded from (`CSkillInfo::LoadSkill`):

| Member | WZ node | Notes |
| --- | --- | --- |
| `nSkillID` | (directory name) | |
| `sName`, `sDescription` | `name`, `desc` | from `String/Skill.img` |
| `nSkillType` | `skillType` | |
| `nPsdSkill` | `psd` | passive |
| `nAttackElemAttr` | `elemAttr` | |
| `nWeapon` | `weapon` | |
| `nSubWeapon` | `subWeapon` | |
| `aAction` | `action` | **array** of action codes |
| `nSpecialAction` | `specialAction` | |
| `nPrepareAction` | `prepare/action` | |
| `tPrepare` | `prepare/time` | |
| `tBallDelay` | `ball/delay`, `flipBall/delay` | |
| `bInvisible` | `invisible` | |
| `bUpButtonDisabled` | `disable` | |
| `nDefaultMasterLev` | `masterLevel` | |
| `bCombatOrders` | `combatOrders` | adds 2 to the allocated level count |
| `bTimeLimited` | `timeLimited` | |
| `dwMobCode` | `mobCode` | |
| `nDelayFrame`, `nHoldFrame` | `specialActionFrame` | |
| `aFinalAttack` | `finalAttack` | |
| `lReqSkill` | `req` | |
| `nMaxLevel` | | derived from the `level` node |
| `common` | `common` | `SKILLLEVELDATACommon`, the formula-driven fallback |
| `aLevelData` / `pLevelData` | `level` | lazily parsed per level |
| `aCharLevelData` / `pCharLevelData` | `CharLevel` | lazily parsed per bracket |
| `bSkillLVData` | `skillLV` | see "CharLevel brackets" |
| `bContinuousEffect` | `effectC` | |
| `sEffectUOL` | `effect` | |
| `sScreenEffectUOL` | `screen` | |
| `sAffectedUOL` | `affected` | |
| `sSpecialAffectedUOL` | `specialAffected` | |
| `sHitRootUOL` | `hit` | the `hit` node itself |
| `asHitUOL` | `hit/<n>` | **array**, one per numbered child |
| `sBallUOL` | `ball` | |
| `sFlipBallUOL` | `flipBall` | |
| `sMobUOL` | `mob` | |
| `sTileUOL` | `tile` | |
| `sAfterimageUOL` | `afterimage` | |
| `sPrepareUOL` | `prepare` | |
| `sKeyDownUOL` | `keydown` | |
| `sKeyDownEndUOL` | `keydownend` | |
| `sSpecialUOL` | `special` | |
| `sSummonedUOL` | `summon` | |
| `sFinishUOL` | `finish` | |
| `apCanvas` | `icon`, `iconDisabled`, `iconMouseOver` | |

Every `s*UOL` is stored as a **path string**, built with one of the format
strings baked into the pool, e.g. `Skill/%03d.img/skill/%07d/effect`,
`Skill/%03d.img/skill/%07d/hit/%d`. There is no structural sniffing anywhere in
the loader: a node either exists at its fixed name or the UOL stays empty.

## SKILLLEVELDATA (per skill level)

`sizeof == 1052`; nearly every numeric field is stored `_ZtlSecureTear`'d (value
split across a value/checksum pair) which is an anti-tamper measure with no
gameplay meaning. Properties read by `SKILLLEVELDATA::LoadLevelData`, in order:

```
hs  action  hp  mp  pad  pdd  mad  mdd  emhp  emmp  epad  epdd  emdd  acc  eva
craft  speed  jump  morph  hpCon  mpCon  moneyCon  itemCon  itemConNo  damage
fixdamage  selfDestruction  time  subTime  prop  subProp  range  mobCount
attackCount  bulletCount  bulletConsume  mastery  x  y  z  cooltime  mhpR  mmpR
cr  criticaldamageMin  criticaldamageMax  accR  evaR  ar  er  pddR  mddR  pdr
mdr  damR  pdR  mdR  padR  madR  expR  dot  dotInterval  dotTime  ignoreMobpdpR
asrR  terR  mesoR  padX  madX  ignoreMobDamR  psdJump  psdSpeed  overChargeR
disCountR  reqGuildLevel  price  lt  rb  ball  flipBall  hit  dateExpire
```

Note that `lt`/`rb` (the affected-area rect, stored in `rcAffectedArea`) and
`range` are separate: `range` is a scalar, `lt`/`rb` are the box.

A level may override the display nodes:

- `level/<n>/ball` → `sBallUOL`
- `level/<n>/flipBall` → `sFlipBallUOL`
- `level/<n>/hit/<i>` → `asHitUOL`

When the `level` node is absent the client synthesises levels from `common`
(`LoadLevelDataByCommon`), which evaluates the arithmetic expressions in
`SKILLLEVELDATACommon`. `GetLevelData(n)` allocates `nMaxLevel` entries, plus 2
more when `bCombatOrders` is set, and parses entry `n-1` on first access.

## CHARLEVELDATA (per character-level bracket)

`sizeof == 32`:

```
asHitUOL  sEffectUOL  sBallUOL  sFlipBallUOL  sSpecialUOL  sAfterimageUOL
nAction  nBaseLevel
```

Loaded from `CharLevel/<baseLevel>/{hit/<i>,effect,ball,flipBall,special,afterimage}`.

### CharLevel brackets

`nBaseLevel` is the child's name parsed as an integer. `GetCharLevelIndex(lv)`
returns the index `i` such that

```
aCharLevelData[i].nBaseLevel <= lv  &&  (i is last || lv < aCharLevelData[i+1].nBaseLevel)
```

and `-1` when nothing matches. So `CharLevel` is a set of **half-open brackets**,
not a lookup keyed on a fixed level. `GetCharLevelData` also drops the last
bracket from consideration when `bSkillLVData` (`skillLV`) is set; in that mode
the caller passes the **skill level** rather than the character level, which is
what `CSkill_HitAni` does when choosing which index to ask for.

## UOL resolution order

This is the part the old client code guessed at, and it differs per node.

**Effect** — `GetEffectUOL(charLevel)`:
1. `CharLevelData(charLevel)->sEffectUOL` if a bracket matches
2. otherwise `SKILLENTRY::sEffectUOL`

**Hit** — `GetHitUOLByIndex(charLevel, skillLevel, index)`:
1. `LevelData(skillLevel)->asHitUOL` if that array is non-empty
2. otherwise `CharLevelData(charLevel)->asHitUOL` if a bracket matches
3. otherwise `SKILLENTRY::asHitUOL`

then index into the chosen array. `GetRandomHitUOL` is the same selection with a
random index.

**Ball** — `GetBallUOL(skillLevel, charLevel, bFlip)`:
1. `CharLevelData(charLevel)` — flip variant when `bFlip` and `sFlipBallUOL` is
   non-empty, else `sBallUOL`
2. otherwise `LevelData(skillLevel)` — same flip rule
3. otherwise `SKILLENTRY` — same flip rule

Note the asymmetry: hits prefer the **skill level** override, balls and effects
prefer the **character level** bracket.

**Action** — `GetRandomAppointedAction(skillLevel, seed)`:
1. `LevelData(skillLevel)->nAction` when it is `>= 0`
2. otherwise `aAction[seed % aAction.size()]`
3. otherwise `-1` (meaning "no appointed action", i.e. use the weapon's regular
   attack action)

There is no two-handed variant selection anywhere in this path. `action/0` and
`action/1` are simply the first two entries of the `aAction` array, and the
client picks between them at random unless the level pins one.

## Hit animation selection (`CSkill_HitAni`)

The constructor dispatches on skill id to one of `CreateFirst`,
`CreateShuffle`, `CreateForFlashRain`, `CreateMultipleLayer` or `CreateDefault`;
all but a small hardcoded id list go to `CreateDefault`.

`CreateDefault` builds one animation per hit in the attack:

1. If the attack uses a cash-item bullet (`is_correct_bullet_cashitem`), use
   `Item/Cash/%04d.img/%08d/hit`.
2. Else, if a skill is involved, take the skill's hit UOL — `GetHitUOLByIndex`
   for a few specific ids, `GetRandomHitUOL` otherwise — passing the character
   level, or the skill level when `bSkillLVData` is set.
3. If that produced an empty string (or there is no skill), fall back to the
   weapon's basic hit: `Character/Afterimage/hit.img/{sword,mace,anniversary}`
   selected by `get_weapon_type`, with `"1"` or `"2"` appended at random, or
   `"F"` when the action is a final attack.

Step 3 is why an attacking skill with no `hit` node still shows a hit spark: the
regular weapon afterimage hit is the fallback, not "no effect".

## Consequences for the WASM client

The previous implementation inferred a skill's behaviour from the *shape* of its
WZ nodes. The real client never does this. Concretely, these were wrong:

- `CharLevel` was read at the hardcoded child `"10"`; it is a bracket search
  over all children keyed by `nBaseLevel`, and `skillLV` can reindex it by skill
  level.
- `effect0`/`effect/1`/"is `effect/0` a bitmap" drove use-effect class choice.
  The loader only ever reads `effect`; there is no multi-effect or two-handed
  effect concept.
- `action/0` + `action/1` were treated as a two-handed pair. They are array
  entries chosen at random, overridden by the level's `action`.
- `level/<n>/ball` and `level/<n>/hit/<i>` overrides were partly handled for
  balls and not at all for hits, and with the wrong precedence.
- `flipBall` and `ball/delay` (`tBallDelay`) were not implemented.
- The `special`/`tile` "emitter" model — `count`/`interval`/`duration`/`start`/
  `fall`/`driftx` spawn scattering, `suppresses_bullet`, `is_single_projectile`,
  `anchors_at_target` — has no counterpart in the client. `special` and `tile`
  are plain UOLs handed to the animation displayer.
- `prepare`/`keydown`/`keydownend`/`finish`/`screen`/`affected`/
  `specialAffected`/`mob`/`summon`/`afterimage`, `specialAction`,
  `specialActionFrame`, `effectC`, `cooltime`, `subWeapon` and `finalAttack`
  were absent entirely.

## The cast state machine

A skill is cast in one of three ways, decided entirely by which nodes it has:

- **Instant** — no `prepare` and no `keydown`. Used and resolved on the same
  frame. This is nearly every skill.
- **Prepare** — has `prepare`, with `prepare/action` and `prepare/time`. The
  wind-up runs first and the skill goes off when it completes.
- **Keydown** — has `keydown`, and `keydownend` to close it out. Charges for as
  long as its key is held, firing repeatedly.

In the v83 game files these are a small, closed set:

| Kind | Skills |
| --- | --- |
| keydown | 2121001, 2221001, 2321001 (Big Bang), 3121004 (Hurricane), 3221001 (Piercing Arrow), 5101004 (Corkscrew Blow), 5201002 (Grenade), 5221004 (Rapid Fire) |
| prepare only | 1121001, 1221001, 1321001 (Monster Magnet), 2111002 (Explosion), 4211001 (Chakra) |

All thirteen appear in Cosmic's `SkillEffectHandler` whitelist, so a client that
sends `SKILL_EFFECT` for exactly the skills carrying these nodes never trips the
server's "entered SkillEffectHandler without being handled" path.

### Server-facing side (Cosmic)

Only one client-to-server message is involved beyond the ordinary attack
packets, and Cosmic already handles it:

| Message | Opcode | Cosmic handler | Payload |
| --- | --- | --- | --- |
| `SKILL_EFFECT` | 93 (`0x5D`) | `SkillEffectHandler` | `int skillId, byte level, byte flags, byte speed, byte aids` |

The server does not act on it; it re-broadcasts the charge to everyone else in
the map. The repeated shots a keydown skill fires are ordinary attack packets on
the existing opcodes (44/45/46), so nothing else on the wire changes.

### Known simplifications

- A prepare cast fires when its wind-up completes; releasing the key earlier
  cancels it. The real client lets a partially charged Explosion go off at a
  lower charge level, which needs charge levels the client does not model.
- Keydown repeat cadence uses the weapon's attack delay with a 120 ms floor,
  rather than the skill's own frame timings.

## The attack pipeline

Recovered from `CUserLocal::TryDoingMeleeAttack` (`0x0091E780`) and the helpers
it calls. This is the layer that turns the data model above into what appears on
screen, and it is where fidelity problems actually live.

| Symbol | Address |
| --- | --- |
| `adjust_rect` | `0x0063C7D0` |
| `CMobPool::FindHitMobInRect` | `0x00657530` |
| `sort_attackInfo_by_distance` | `0x00904A50` |
| `CMob::GetHitPoint` | `0x00642260` |
| `CMob::LoadEffectLayer` | `0x006458E0` |
| `CMob::GetRandomHitAction` | `0x00639F70` |
| `get_attack_speed_degree` | `0x006EE880` |
| `CUser::GetShootDelay` | `0x008DE240` |
| `CUser::GetDamageDelay` | `0x008E0FF0` |
| `CUser::GetBulletDelay` | `0x008DE2D0` |
| `CUser::IsFanShapeShoot` | `0x008DE410` |
| `is_shoot_skill_not_showing_bullet` | `0x006EDC50` |
| `get_vertical_adjust_of_attack_range` | `0x006ED830` |
| `is_attack_area_set_by_data` | `0x006ED8E0` |
| `GetShootRange0` | `0x00903230` |
| `CMobPool::FindHitMobInTrapezoid` | `0x00657930` |
| `CMobPool::FindHitMobInRect` | `0x00657530` |
| `is_rect_attack_shoot_skill` | `0x006ED980` |
| `is_guided_bullet_skill` | `0x006ED800` |
| `CAnimationDisplayer::RegisterBulletAnimation` | `0x00455410` |
| `NormalBullet::PrepareBulletLayer` | `0x0044C380` |
| `GetShootAttackPt0` | `0x009039A0` |
| `CSkillInfo::GetShootSkillRange` | `0x00709650` |
| `is_cygnus_job` | `0x0047CA80` |
| `CMob::AddDamageInfo` | `0x00653A10` |
| `CMob::OnHit` | `0x006534DD` |
| `CMob::ShowDamage` | `0x0063C950` |
| `CalcDamage::PDamage` | `0x00730130` |
| `SKILLLEVELDATA::LoadLevelData` | `0x00700990` |
| `1.5` (ms per pixel of shot travel) | `0x00B51FC8` |

### Facing

`Char::set_direction(true)` is what the **right** key sets, so `flip` is true
when facing right and `Attack::toleft` (`!flip`) is true when facing left. The
hit rect must therefore extend towards -x when `toleft` is set. Every skill rect
in the game files is symmetric about x, so an inverted orientation stays hidden
until something makes the span one-sided - such as applying the forward reach
below.

### Target selection

```c
adjust_rect(rect, x, y, bLeft) {
    if (bLeft) { new.left = -old.right; new.right = -old.left; }
    OffsetRect(rect, x, y);
}
```

The level's `lt`/`rb` describes a **right-facing** attack area in character-local
coordinates; facing left mirrors it about x=0. It is then offset to the caster
and handed to `FindHitMobInRect`, capped at `nMobCount`.

`sort_attackInfo_by_distance(bLeft, infos, count)` orders the results by
`ptHit.x` - ascending facing right, descending facing left - so hits resolve
nearest-first along the facing direction.

Each `ATTACKINFO` carries its own `tDelay`, so per-target stagger is real and
comes from the attack data rather than a constant.

### Hit animation placement

`CMob::GetHitPoint(rect)` returns the centre of `IntersectRect(attackRect,
mobBodyRect)` - the point where the swing actually met the monster, stored in
`ATTACKINFO::ptHit`.

Separately, a hit animation's own `pos` property selects its anchor on the
target. `CMob::LoadEffectLayer` branches on exactly these values:

| `pos` | Anchor |
| --- | --- |
| 0 | the monster's position vector |
| 1 | `m_pvcHead`, the monster's head |
| 2 | centre of the body rect, relative to origin; nothing is drawn if that rect is empty |
| 3 | `GetVecCtrl`, the control point |
| 4 | not branched on, so it falls through to the default |

41 skills in the v83 files use a non-zero `pos`; the property is only written
when non-zero, so an absent `pos` means 0.

### Reach of a fired attack

`CSkillInfo::GetShootSkillRange(charData, skillId, weaponType)` (`0x00709650`):

```c
if (levelData.nRange > 0) return nRange;          // the level's scalar "range"
if (skillId / 1000000 == 33) return 420;
switch (weaponType) {
  case 45: case 46: return 300 + masteryRange;    // bow, crossbow
  case 47:          return 200 + masteryRange;    // claw
  case 49:          return 200;                   // gun
}
```

This is the distinction that matters: for anything **fired**, how far the attack
travels comes from the level's **scalar `range`**, not from `lt`/`rb`. The rect
describes the band the shot sweeps and the area a burst covers where it lands.
Treating the rect as the reach confines the attack to whatever is already beside
the caster, which is what broke Inferno.

The mastery-skill bonus added for bows and claws is not implemented here.

### Emitted area effects

A `special` node carrying `count` is an **emitter**, not a single animation. Its
numbered children are interchangeable animations and the siblings are the
stream's parameters. `CAnimationDisplayer::RegisterFallingAnimation`
(`0x00459B40`) reads them into a `FALLINGINFO`:

| WZ property | FALLINGINFO field |
| --- | --- |
| `x` | `nX` |
| `y` | `nY` |
| `a` | `nAlpha` |
| `fall` | `tFall` |
| `interval` | `tUpdateInterval` |
| `count` | `nUpdateCount` |
| `start` | `tUpdateNext` (plus now, plus the caller's delay) |
| `duration` | `tEnd` (plus now, plus the caller's delay) |
| `<n>` | `apProperty[]` |

`FALLINGINFO::Update` (`0x0044D8B0`) then runs:

```c
if (now > tEnd) return done;
if (now >= tUpdateNext) {
    tUpdateNext += tUpdateInterval;
    for (i = 0; i < nUpdateCount; i++) {
        y = rcStart.top  + rand() % (rcStart.bottom - rcStart.top);
        x = rcStart.left + rand() % (rcStart.right - rcStart.left);
        prop = apProperty[rand() % apProperty.size()];
        dy = nY;
        if (nX) dy = ((nX - (rand()%41 + nX - 20)) * nX) / nY + nY;   // nY +/- jitter
        tfall = tFall + rand()%300 - 150;
        dx = bLeft ? -nX : nX;
        RelOffset(layer, dx, dy, tfall);
        alpha = nAlpha ? nAlpha : 128 + (rand() & 63);
        alpha ramps over 150 ms; layer animates GA_REPEAT for tfall
    }
}
```

The consequences, none of which are obvious from the field names:

- `count` is copies **per tick**, and the emitter ticks every `interval` ms from
  `start` until `duration`. Arrow Rain (count 8, interval 90, duration 500) emits
  roughly **48** arrows, not 8.
- `fall` is a **time in ms**, not a distance, and is jittered by +/-150.
- `x` and `y` are the **travel vector**, not a spread or a start height. `x` is
  negated when facing left.
- Copies spawn at a **uniformly random point inside the rect** the caller passes,
  which is the affected area - the rect is the spread.
- The animation is chosen at random per copy from all numbered children.
- `a` is the opacity, defaulting to a random 128-191 per copy.

Arrow Eruption's `x=0, y=-150` therefore rises straight up, and Arrow Rain's
`x=85, y=230` falls while drifting sideways.

### Area effects: three emitters, not one

`special` and `tile` are handed to one of **three** different registrars on
`CAnimationDisplayer`, each with its own geometry. Which one is used depends on
the attack path, and the rect and delay always come from the caller.

| Registrar | Address | Used by | Reads |
| --- | --- | --- | --- |
| `RegisterFallingAnimation` | `0x00459B40` | shoot skills' `special` | `x y a fall interval count start duration` |
| `RegisterExplosionAnimation` | `0x0045A1D0` | magic skills' `special` | `interval count start duration` |
| `RegisterFootHoldAnimation` | `0x0045A990` | `tile` on both paths | `effectDistance` |

#### Falling (`FALLINGINFO::Update`, `0x0044D8B0`)

Covered above: copies spawn at a random point in the rect, travel `(x, y)` over
`fall` ms (a time), repeat their animation, and fade in to alpha `a`.

#### Explosion (`EXPLOSIONINFO::Update`, `0x0044DDF0`)

```c
// registration
nX = (rect.left + rect.right) / 2;   nY = (rect.top + rect.bottom) / 2;
nWidth  = rect.width();   nCurWidth  = nWidth  * 5 / 6;
nHeight = rect.height();  nCurHeight = nHeight * 5 / 6;
tUpdateNext = start + now;   tEnd = duration + now;    // no caller delay

// update
if (now > tEnd) return done;
if (now >= tUpdateNext) {
    dw = nWidth  - nCurWidth;   dh = nHeight - nCurHeight;   // band, before shrink
    nCurWidth  = nCurWidth  * 3 / 4;                          // shrinks each tick
    nCurHeight = nCurHeight * 3 / 4;                          // so the band grows
    tUpdateNext += tUpdateInterval;
    for (i = 0; i < nUpdateCount; i++) {
        x = nX + rand()%dw - dw/2;
        y = nY + rand()%dh - dh/2;
        layer = LoadLayer(apProperty[rand() % size], x, y);
        layer.Animate(GA_NORMAL);        // plays once, unlike the falling copies
        RegisterOneTimeAnimation(layer, 0);
    }
}
```

The band starts at one sixth of the rect and widens every tick, so the copies
burst outward from the centre. There is no travel, no `fall`, and no alpha ramp -
each copy simply plays once where it appears.

#### Foothold (`RegisterFootHoldAnimation`)

```c
effectDistance = prop["effectDistance"];
info.tStart = delay + now;
info.tEnd   = info.tStart + duration;
info.a0 = alphaStart;   info.a1 = alphaEnd;
MakeLayer_FootHold(name, rect, &info.apLayer, effectDistance, flag);
```

`effectDistance` is the spacing between tiles laid along the ground across the
rect. Observed call arguments: Inferno passes `(delay, 500, 0x80, 0xff, 0)`, the
magic path passes `(180, 3000, 0x80, 0xff, 0)` - so the duration is a per-skill
constant in the caller, **not** the level's `time`.

### Where an emitter is anchored

`CUserLocal::TryDoingShootAttack` (`0x00925A00`) and
`CUserLocal::TryDoingMagicAttack` (`0x0092A240`) each dispatch on skill id.

**At the first mob the shot hits** - Inferno (3111003), Blizzard (3211003):

```c
rect = levelData.rcAffectedArea;
OffsetRect(&rect, hitPoint.x, hitPoint.y);      // OffsetRect: no facing mirror
if (skill == Inferno) RegisterFootHoldAnimation(GetTileUOL(), rect, delay, 500, 0x80, 0xff, 0);
rect.top -= 50; rect.bottom -= 170;
RegisterFallingAnimation(GetSpecialUOL(), bLeft, rect, delay);
```

`hitPoint` is `CMob::GetHitPoint(hitMobs[0])` - the **first** mob along the shot -
and the same block sets the target count to 1.

**At the caster** - Arrow Rain (3111004):

```c
adjust_rect(&rect, casterPos.x, casterPos.y, bLeft);   // mirrored by facing
rect.top -= 250; rect.bottom -= 250;                   // lifted above the caster
RegisterFallingAnimation(GetSpecialUOL(), bLeft, rect, delay);
```

**Per struck mob** - Arrow Eruption (3211004):

```c
for (each hit mob) {
    GetBodyRect(mob, &rect, 0);
    if (rect.width() < 150) widen symmetrically to 150;
    rect.top = rect.bottom - 10;                        // thin band at its feet
    RegisterFallingAnimation(GetSpecialUOL(), bLeft, rect, delay);
}
```

**Magic, at the caster** - Explosion (2111002):

```c
rect = levelData.rcAffectedArea;
OffsetRect(&rect, casterPos.x, casterPos.y);
RegisterExplosionAnimation(GetSpecialUOL(), rect);
```

Big Bang (2121001, 2221001) and Blizzard (2221007) fall through to a shared tail
that uses `adjust_rect` at the caster and shifts the rect up by 40 before
calling `RegisterExplosionAnimation`.

One further detail: `sort_attackInfo_by_distance` is **skipped** for Inferno,
Blizzard and Arrow Bomb among others, so those keep their original target order.

### When a target is hit, and when the emitter starts

The value the registrars are handed as their delay is **not** a separate
animation delay. Ghidra names it as its own local because the store goes through
a computed pointer, but the stack slot it reads is `EBP-0xB0C`, which is
`aAttackInfo[0]` (`EBP-0xB1C`) plus `ATTACKINFO::tDelay` at offset `0x10`:

```
00927eb1: MOV EDX,dword ptr [EBP + 0xfffff048]   ; &aAttackInfo[i]
00927eb7: MOV dword ptr [EDX + 0x10],ECX         ; ATTACKINFO::tDelay
...
009281ed: MOV ECX,dword ptr [EBP + 0xfffff4f4]   ; aAttackInfo[0].tDelay
009281f3: PUSH ECX                               ; -> RegisterFallingAnimation
```

So **the area animation starts on the frame the first target takes its damage.**
The registrar adds that delay to both ends of the emitter's window
(`tUpdateNext = start + now + delay`, `tEnd = duration + now + delay`), so
`start` and `duration` are offsets from the hit, not from the packet.

`RegisterExplosionAnimation` takes no delay parameter at all, so the magic path's
bursts do start the moment the attack is applied.

#### The base

Both reversed paths open with the same expression:

```c
base = (s_aCharacterActionData[action].tEventDelay
        * CAvatar::GetActionInfo()->tTotFrameDelay)
       / s_aCharacterActionData[action].tTotalDelay;
```

That is how far into the action the frame flagged as its event sits, rescaled
from the action's nominal length to the length the animation actually runs for -
i.e. the attack-speed scaling. The shoot path then passes it through
`CUser::GetShootDelay` (`0x008DE240`), which replaces it outright for a short
list of ids (`5001003`/`5210000` → 90, `5221007` → 180, `5221008` → 600,
`13111006` → 618, `13111007` → 561, `15111007` → 449) and returns the skill's
`ballDelay` for `3221001`, `33101001` and `33121005`.

#### `ATTACKINFO::tDelay`, per target

Shoot path (`0x00927E84`-`0x00928024`), target index `i`:

| Skills | `tDelay` |
| --- | --- |
| 3101005 Arrow Bomb, 33101007 | `i == 0`: as below; `i > 0`: `tDelay[0] + 200` |
| 3111003 Inferno, 3211003 Blizzard | `i == 0`: as below; `i > 0`: `tDelay[0] + 120 + 30i` |
| 3111004 Arrow Rain, 3211004 Arrow Eruption, 13111000 | `base + 400`, every target |
| 35121012 | `base` |
| everything else | `base + \|hitPoint.x - casterPos.x\| * 1.5` |

Magic path (`0x0092B512`-`0x0092B649`):

| Skills | `tDelay` |
| --- | --- |
| 2121007, 2221007, 2321008, 12111003, 32121004 | `base` |
| 2101004, 2121003, 12111006, 22101000, 32111003 | `base + \|casterPos.x - mobPos.x\| * 1.5` |
| everything else | `base + 50i` |

The `1.5` is the double at `0x00B51FC8`, shared by both paths - the shot covers
a pixel in 1.5 ms. Chain lightning uses the same shape with the Euclidean
distance from the previous link instead.

Consequences worth stating plainly:

- **Arrow Rain does not start with the swing.** Its arrows and its damage both
  land at `base + 400`. Emitting from `start` alone puts the whole volley
  roughly 400 ms early.
- **Inferno's damage is the special appearing.** The first monster's `tDelay` is
  the arrow's flight time, and that same number is the emitter's delay, so the
  flames and the first damage number are the same frame by construction. The
  rest of the spread trails it by 120 ms and then 30 ms apart.
- One `ATTACKINFO` covers one monster and carries one `tDelay`, so every damage
  line a monster takes lands together, however many times it was struck.
- The shoot path's falling emitters are all registered inside a
  `if (hitMobCount != 0)` block: a volley that connects with nothing paints no
  arrows. The magic path's bursts are not gated.

`ATTACKINFO::tDelay` is overridden entirely for a few ids by
`CUser::GetDamageDelay` (`0x008E0FF0`), which indexes a per-skill table
(`aDelay_Fist`, `aDelay_Vampire`) and scales the result by
`(get_attack_speed_degree(...) + 10) / 16`. It returns `-1`, meaning "use the
rule above", for everything else.

### What a fired attack can reach

There are **two** target searches, chosen by `is_rect_attack_shoot_skill`
(`0x006ED980`). The wedge is the one almost every bow skill uses; the rect is the
exception.

#### The wedge (`0x00926427`-`0x0092683b`)

```c
if (!is_rect_attack_shoot_skill(skill)) {
    mob = 0;
    if (!is_guided_bullet_skill(skill))                       // 0x006ED800
        mob = FindHitMobInTrapezoid(shootPt.x, nearX, farX, shootPt.y, 4, 1);
    if (!mob)
        mob = FindHitMobInTrapezoid(shootPt.x, nearX, farX, shootPt.y, 4, 0);

    if (mob && !CanGoThrough(shootPt, GetHitPoint(mob)))  mob = 0;   // terrain

    if (mob) {
        hitPoint   = CMob::GetHitPoint(mob, rect);
        attackPt   = hitPoint;          // the shot is aimed AT the monster
        hitCount   = 1;
        m_pvcTarget = mob->GetVecCtrl();
        if (splash skill) {             // 3001004, 3101005, 3111003, 3211003,
            rect = levelData.rcAffectedArea;     // 33101007, 35111004,
            OffsetRect(&rect, hitPoint.x, hitPoint.y);  // 35121005, 35121013
            hitCount += FindHitMobInRect(rect, &mobs[1], mobCount - 1, /*skip*/mobs[0]);
        }
    }
}
```

`CMobPool::FindHitMobInTrapezoid` (`0x00657930`) walks outward from the muzzle in
20 px slices. Slice at distance `d` is a band reaching `d / 4` above and below the
muzzle line, and the first monster any slice touches wins:

```c
for (x = nearX; x != farX; x += 20 * dir) {
    rect = { x, muzzleY - |x - muzzleX|/4, x + 20*dir, muzzleY + |x - muzzleX|/4 };
    if ((mob = FindHitMobInRect(rect, 1, ...))) return mob;
}
```

So a shot forgives more height the further it travels - roughly +/-75 px at 300,
+/-105 at the maximum 420 - and it hits **one** monster however many stand behind
it. That is why Strafe and Hurricane put all their damage on a single target, and
why Arrow Bomb and Inferno need the second, rect-based search to spread. It is
also where the vertical aiming comes from: the attack point becomes the monster's
own hit point, so the arrow angles up or down towards it.

The two calls differ only in the last argument: the first restricts the search to
the character's current target (read from `CUserLocal + 0x13E4`), the second
takes anything.

#### The rect

`is_rect_attack_shoot_skill` is true for Power Knock-Back (3101003, 3201003),
Arrow Rain, Arrow Eruption, Iron Arrow, Dragon's Breath, Piercing Arrow, Avenger,
both Taunts, and a run of Pirate/Cygnus/Mechanic ids - the union of everything
`get_vertical_adjust_of_attack_range` and `is_attack_area_set_by_data` name,
which is what makes those two functions matter. For these,
`CUserLocal::TryDoingShootAttack` draws a line from the muzzle and asks which
monsters cross it (`0x00926427`-`0x0092676b`):

```c
shootPt = GetShootAttackPt0(skill, job, casterPos, ...);   // 0x009039A0
near    = shootPt.x +/- GetShootRange0(skill, ...);        // 0x00903230
far     = shootPt.x +/- GetShootSkillRange(...);           // 0x00709650
rect    = { min(near,far), shootPt.y, max(near,far), shootPt.y + 1 };

vadj = get_vertical_adjust_of_attack_range(skill);         // 0x006ED830
if (vadj >= 1)
    rect.top -= vadj, rect.bottom += vadj;
else if (is_attack_area_set_by_data(skill))                // 0x006ED8E0
    rect = adjust_rect(levelData.rcAffectedArea, casterPos, bLeft);

hitCount = CMobPool::FindHitMobInRect(rect, mobCount);
```

- `GetShootAttackPt0` returns `(casterPos.x, casterPos.y - 28)` for the bow line;
  a few Wild Hunter and Mechanic ids shift it further.
- `GetShootRange0` returns 0 for the bow line, so the near edge is the caster.
- The rect is **one pixel tall** unless the skill widens it. That is enough: the
  monster's own bounds are what the line is tested against.
- `get_vertical_adjust_of_attack_range`: 20 for Arrow Blow, Dragon's Breath,
  Piercing Arrow and 33101001; 10 for Iron Arrow, both Taunts and 13111006; 36
  for Avenger and 14111002; 12 for 33121005; 60 for 11101004, 15111007, 21100004
  and 21110004; 150 for 15111006; **0 for everything else**, Strafe and Inferno
  included.
- `is_attack_area_set_by_data`: Power Knock-Back (3101003, 3201003), Arrow Rain,
  Arrow Eruption, and a dozen Pirate/Cygnus/Mechanic ids. Note that **Inferno and
  Arrow Bomb are not on it** - their `lt`/`rb` describes where the burst lands,
  not what the shot can reach.

#### `CSkillInfo::GetShootSkillRange` (`0x00709650`)

```c
if (skill's level data nRange > 0)   return nRange;      // no mastery bonus
if (skill in {4121003, 4221003, 33121005}) return 300;
if (skill / 1000000 == 33)           return 420;
if (skill == 35111015)               return abs(rcAffectedArea.left) - 60;
switch (weaponType) {                 // itemid/10000 - 100
case 45: case 46:                     // bow, crossbow
    return 300 + level_of(cygnus ? 13000001 : 3000002).nRange;
case 47:                              // claw
    return 200 + level_of(cygnus ? 14000001 : 4000001).nRange;
case 49: return 200;                  // gun
}
```

`is_cygnus_job(job)` is `job / 1000 == 1`. The bow bonus skill is **The Eye of
Amazon (3000002)**, whose level data carries `range` directly - `+15` per level
up to `+120` at level 8, i.e. a maximum reach of **420**. Strafe, Inferno,
Hurricane and the plain bow attack all state no range of their own, so every one
of them depends on this bonus; without it they stop at 300.

### Projectiles

The number of projectiles a ranged attack looses is the level's `bulletCount`,
independent of how many monsters are hit. One arrow passing through six mobs is
one arrow. Spawning a projectile per target is what makes a piercing skill spray.

Three helpers decide what the volley looks like:

| Helper | Address | Answers |
| --- | --- | --- |
| `is_shoot_skill_not_showing_bullet` | `0x006EDC50` | draw a projectile at all? |
| `CUser::GetBulletDelay` | `0x008DE2D0` | gap between the shots of one volley |
| `CUser::IsFanShapeShoot` | `0x008DE410` | spread the volley vertically? |

**No projectile** for `nSkillType == 3` or for 3101003, 3111004 Arrow Rain,
3201003, 3211004 Arrow Eruption, 5201001, 5211004, 5211005, 13101005, 13111000,
14101006, 21120006, 33101002, 33121001, 35001001, 35101009, 35121012. Arrow Rain
and Arrow Eruption still arm and spend an arrow; they simply do not show one.

**Volley gap**, in ms:

| Condition | Gap |
| --- | --- |
| bullet id `207xxxx` or `5021xxx`, or skill 4111004 or 5221007 | 120 |
| 3111006 Strafe, 3211006, 13111001, 33001000, 33111001 | 60 |
| 35001004, 35101010 | 90 |
| 5001003, 5210000 | 240 |
| everything else | **0** - the whole volley leaves at once |

The gap is a per-skill constant, not something derived from the attack
animation's frames.

**Fan**: `IsFanShapeShoot` is true for everything except five Mechanic ids, and
shifts shot `i` of `n` by `(2i - (n - 1)) * 7` pixels vertically - a 14 px step
centred on the aim point.

**Aim point**: every shot of a volley is aimed at the **attack point**, which the
wedge search has already replaced with the struck monster's hit point. A volley
that connects therefore flies at that monster, at its height; one that finds
nothing keeps the flat `(shootPt.x +/- reach, shootPt.y)` and stops visibly short
of whatever was out of reach. Only post-Big-Bang Arrow Blow (3001004) re-aims
shot `i > 0` separately, at `hitMobs[i]`.

**Rotation**: the only angle the client applies is `rotatePeriod` (string id
2848) off the bullet-effect node - a constant spin, negated when facing left. It
never turns a shot towards what it was fired at. We do, because an arrow angled
up a slope but drawn level reads as a bug.

**Flight**: `flight = (long)(sqrt(dx*dx + dy*dy) * 1.5)` from the muzzle to the
aim point, using the same `0x00B51FC8` constant as the damage delay. Shot `i`
starts at `shootDelay + gap*i` and lands at `+ flight`. Since a target's damage
is charged at `shootDelay + |dx| * 1.5`, each monster takes its damage on the
frame the shot passes it - the two are the same constant, not a coincidence.

### Damage numbers

`ATTACKINFO::tDelay + now` is handed to `CMob::AddDamageInfo` (`0x00653A10`) once
per damage line, and `CMob::Update` (`0x00654515` region) drains **every**
`DAMAGEINFO` whose `tDelayedProcess` has come due in a single tick, calling
`CMob::OnHit` -> `CMob::ShowDamage` for each. There is no per-hit stagger: one
monster's damage lines all pop on one frame, however many times it was struck.
The only exception in the binary is skill 22171002, whose hit index indexes a
`{0, 60, 180, 420}` table inside `AddDamageInfo`.

So a four-arrow Strafe shows four numbers at once while its four arrows leave
60 ms apart. **This is what `GMSv95.exe` does; whether v83 staggered them is not
something this binary can answer.**

We depart from it deliberately: damage line `i` on a monster lands at
`tDelay + gap * min(i, shots - 1)`, so each number appears as the shot carrying
it passes. A volley that leaves in sequence and lands in one lump does not read
as four arrows hitting.

### Skill damage rate

`CalcDamage::PDamage` (`0x00730130`) takes the rate from
`SKILLENTRY::GetLevelData(skill, level)->nDamage` and then adds per-skill
adjustments. `SKILLLEVELDATA::LoadLevelData` (`0x00700990`) reads `damage` into
`nDamage` at struct offset `0xD0` and `x` into `nX` at `0x160` - **two separate
fields, with no fallback between them**. A skill whose level data has no `damage`
therefore uses the default rate of 100%, not its `x`. Arrow Bomb is the case that
matters: its levels carry only `x` (90 at level 10, matching the tooltip's
"damage 90%"), and reading that as the rate is wrong - the server assumes 100%
for it as well. Reading a missing `damage` as **zero** is what made it hit for 1.

### Skills that swing rather than fire

Power Knock-Back (3101003, 3201003) goes through `DoActiveSkill_ShootAttack` like
any bow skill, spends an arrow, and draws none. What makes it a swing is its own
`afterimage` node, whose children are `swingT1` and `swingT3` - the two-handed
swing stances - carrying `lt`/`rb` of `(-100,-60)`-`(0,0)`. That rect is the
reach, the way an afterimage's rect is for any close attack; the level's
`range = 130` describes a shot it never fires.

Across `Skill.wz` the skills that both hide their bullet and carry an afterimage
are exactly these two, so "no projectile plus its own afterimage" identifies a
swing without a table. Rapid Fire (5221004) has an afterimage but does draw a
bullet; Arrow Rain has neither.

The v95 tables put Power Knock-Back in `is_rect_attack_shoot_skill` **and** in
`is_attack_area_set_by_data`, which together say "take your targets from the
level's `rcAffectedArea`" - but the v83 levels have no `lt`/`rb`, so that rect is
empty and the skill lands nothing at all. Post-Big-Bang the skill does not exist
(310.img has no 3101003), so that branch is vestigial code written against data
we do not have. What the v83 levels do carry is `range` (130 rising to 150),
`mobCount`, `prop` as the knock-back rate and `damage` - and the tooltip reads
"knock-back +40%, damage 200%, knock-back 6 enemies" - so the box runs `range`
forward over the band the swing afterimage covers.

Two details are ours, not the client's: the swing is pinned to `swingT1` rather
than picked between `swingT1` and `swingT3`, and the shove distance is a
constant. The client takes the latter from `SKILLLEVELDATA::nX`
(`nMoveType = 5`, ending x = `casterX +/- nX + rand(-10..10)`, set at
`0x00928F2E`), and these skills carry no `x`.

### How a rect is placed

`adjust_rect` (`0x0063C7D0`) is the whole of it:

```c
if (mirror) { swap and negate the x edges: left = -right, right = -left; }
OffsetRect(rect, pos.x, pos.y);
```

and every caller passes `mirror = (IsLeft() == 0)`. So **rects are written facing
left and mirrored when the character faces right**, not the other way round. It
makes no difference to the skill rects, which are symmetric about x, but the
afterimage rects are not - every one in `Character/Afterimage` runs from `-w` to
`0` (bow `swingT1` is `lt (-30,-81)`, `rb (0,5)`; sword `swingO1` is `lt
(-85,-51)`, `rb (-11,-11)`). Mirroring those the wrong way puts a close attack's
whole box behind the character, which is what it was doing.

### Attack speed

`get_attack_speed_degree(base, skillId, booster, ...)` sums the base speed,
booster and modifiers, then clamps the result to **[2, 10]**. A handful of skill
ids zero the booster contribution first.

`CMob::GetRandomHitAction()` is `rand() % nHitCount + 7`, i.e. the monster's hit
reaction actions start at index 7.

## Passives and buffs

A skill is passive when `(id % 10000) / 1000 == 0`, and the client applies every
passive the character has a level in on each stat recalculation. Nothing is sent
for one and nothing is received: the level's properties are read straight out of
`Skill.wz` and folded into the character's totals.

Which property means what is per skill and is not derivable, so it is a table.
For the bow line:

| Skill | Properties | Effect |
| --- | --- | --- |
| 3000000 The Blessing of Amazon | `x` | accuracy |
| 3000001 Critical Shot | `prop`, `damage` | critical rate, critical damage |
| 3000002 The Eye of Amazon | `range` | added to the shoot reach (see above) |
| 3100000 Bow Mastery | `mastery`, `x` | mastery, accuracy |
| 3100001 Final Attack: Bow | `prop`, `damage` | chance, damage of the follow-up |
| 3110000 Thrust | `speed` | speed |
| 3120005 Bow Expert | `mastery`, `x` | mastery, **weapon attack** |

`mastery` is in 5% steps above a 10% unmastered floor, so `0.1 + mastery * 0.05`:
Bow Mastery's 1-10 gives 15%-60% and Bow Expert's 11-16 continues the same scale
to 65%-90%. Bow Expert's `x` is the one place a mastery skill's `x` is not
accuracy. Both apply at once and the higher mastery wins.

### Final attack

`SKILLENTRY::aFinalAttack` (the `finalAttack` node) is a list of follow-up
skills, each carrying the weapon types it covers. `CUserLocal::TryDoingShootAttack`
(`0x00929f2c`) and `TryDoingMeleeAttack` (`0x00923c8e`) call
`TryRegisterFinalAttack` (`0x00904e00`) with it - but only when a skill was
swung, so a plain attack never sets one off. Ten skills carry the node; in the
bow line Arrow Blow, Double Shot and Arrow Bomb all chain into 3100001 for a bow
and 3200001 for a crossbow.

`TryRegisterFinalAttack` rolls `rand() % 101 <= prop` and records the pending
attack with `tStart = now + delay`, where the caller's delay is

```c
base + (CAvatar::GetActionInfo()->tTotFrameDelay - base) / 3
```

`CUserLocal::TryDoingFinalAttack` (`0x0093aaa0`) then re-checks that the weapon
has not changed and dispatches by weapon type: melee types go through
`TryDoingMeleeAttack`, bow/crossbow/claw/gun through `TryDoingShootAttack` with
action `0x41`. So a final attack is an ordinary attack packet carrying the final
attack's own skill id, and a fired one spends an arrow like any other shot -
Cosmic charges for one too.

### Buffs on the wire

`GIVE_BUFF` opens with two 64-bit masks and then writes one `short value, int
skillid, int duration` record per bit set. The records are unlabelled, so they
must be read in the order the server wrote them. Cosmic appends them in a fixed
sequence rather than sorting by bit:

1. `AURA`, `MAP_PROTECTION`, and the item-only recovery and coupon stats
2. the shared equip block: `WATK WDEF MATK MDEF ACC AVOID SPEED JUMP`
3. whatever the skill's own case in `StatEffect` adds
4. `MORPH`, `GHOST_MORPH`

Group 2 preceding group 3 is what makes the order matter rather than being an
accident: Concentrate sends `WATK` (its `pad`) before `CONCENTRATE` (its mana
saving), which is the opposite of their bit order. `Buffstat::codes` holds this
sequence.

Several stats share a bit - a summon and a combo count, a puppet and pickpocket
in the client's own tables - so each mask appears exactly once in that list.
Reading a shared bit twice would consume two records where the server wrote one
and misread everything after it.

The archer buffs and what they carry:

| Skill | Stats sent | Value |
| --- | --- | --- |
| 3001003 Focus | `ACC`, `AVOID` | the level's `acc` and `eva` |
| 3101002 Bow Booster | `BOOSTER` | `x`, the attack-speed steps gained (-2) |
| 3101004 Soul Arrow | `SOULARROW` | `x` |
| 3111002 Puppet | `PUPPET` | 1; the level's `x` is the puppet's hp |
| 3121000 Maple Warrior | `MAPLE_WARRIOR` | `x`, a percentage of every base stat |
| 3121002 Sharp Eyes | `SHARP_EYES` | `x << 8 \| y` - rate in the high byte, critical damage in the low |
| 3121007 Hamstring | `HAMSTRING` | `x`, the speed it takes off a monster |
| 3121008 Concentrate | `WATK`, `CONCENTRATE` | `pad`, then `x` as a mana saving |

Sharp Eyes' low byte is the whole of a critical (111-140), not the bonus over a
normal hit, so only the part above 100 adds to what Critical Shot set.
Hamstring's and Concentrate's own values are server-side effects - a monster
status and a skill's mana cost - and change nothing the client computes.

## Not covered here

Still approximated rather than reversed:

- **The base delay.** The client scales the action's nominal event delay by the
  animation's real length; we use `Char::get_attackdelay(0)`, which is the same
  quantity taken from the same frame data but scaled by our own attack-speed
  factor. The per-target rules built on top of it are reproduced exactly.
- **Projectile flight.** `RegisterBulletAnimation` hands a `NormalBullet` a
  start time, an arrival time and the target's vector; how `CBullet` steps
  between them has not been reversed. Ours covers the gap in
  `distance * 1.5 ms`, which is the arrival time the caller computes, so the
  shot reaches its target on the frame that target takes its damage. Turning
  the sprite along its flight line is ours; the client only spins by
  `rotatePeriod`.
- **`CanGoThrough`.** The client drops a target the terrain blocks the line to.
  We do not test terrain, so a shot can connect through a wall.
- **Close attacks.** `TryDoingMeleeAttack` fills in `tDelay` too, and that has
  not been reversed, so a close attack still spreads its hits over the swing
  and cascades targets by a tuned constant.
- **Which skills attack.** The reference client answers this with a per-id
  switch in `CUserLocal::DoActiveSkill` thousands of cases long. We derive it
  from the data instead: a skill attacks if any level states `damage`, `mad` or
  `fixdamage`, **or** it carries a `hit` node - the animation played on a
  monster it strikes. That last clause is what makes Arrow Bomb, Snipe, Shadow
  Meso, Meso Explosion and the event skills work; they keep their damage
  percentage in `x`, which the tooltip strings confirm (Arrow Bomb level 10 has
  `x = 90` against "damage 90%"). Across the whole of `Skill.wz` exactly four
  non-summon skills carry a `hit` node without attacking - Heal, both Taunts and
  Hypnotize - and those are listed as exceptions.

### Version mismatch

`GMSv95.exe` is post-Big Bang; the `wz/` data the client runs on is v83. The
binary's dispatch tables still carry the v83 ids (3111004 Arrow Rain, 3111006
Strafe, 3101003 Power Knock-Back), so they transfer directly. **Double Shot
(3001005) is dispatched by v95's `DoActiveSkill` but appears in none of the
timing tables**, so its 60 ms volley gap is applied by analogy with every other
multi-arrow bow skill rather than sourced. Post-Big Bang the special case at that
end of the bow line is Arrow Blow (3001004), which fires one arrow per monster it
hits - v83 Arrow Blow is single-target, so that branch does not apply either.

Note also that the v83 data lacks fields the v95 tables assume: Power Knock-Back
is in `is_attack_area_set_by_data`, but its v83 levels carry no `lt`/`rb` for
that branch to use, which is why its reach comes from the afterimage instead.
- **`ptHit`.** Hit animations are placed by the target's own `pos` anchor, not
  at the rect/body intersection the client computes. Plumbing `ptHit` through
  needs the attack rect to reach `apply_hiteffects`, which it currently does not.
  The impact-anchored emitters use the struck monster's body position for the
  same reason, where the client uses `GetHitPoint`.
- **The melee path.** `TryDoingMeleeAttack`'s anchoring has not been reversed, so
  melee area skills - Brandish, Rush, Dragon Roar, Meso Explosion - still fall
  through to the single-animation case.
- **Tiles.** `RegisterFootHoldAnimation` anchors a skill's `tile` to the ground
  geometry; ours places it at an anchor point. Its delay is reproduced (the
  first target's hit delay on the shoot path, a flat 180 ms on the magic one).
- Attack response packets, summon lifetimes, and the affected-area
  (`affected` / `specialAffected`) overlays applied to buffed characters.
