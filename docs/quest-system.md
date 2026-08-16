# Quest system

How questing works in this client, what was reverse-engineered to build it, and
what is still open.

The questing system did not exist before this work: `Questlog` was a stub that
stored data nothing read, and `MapNpcs::send_cursor` carried a
`// TODO: try finding dialogue first`.

## The central fact

**The server never offers a quest.** Cosmic's `NPCTalkHandler` runs an npc
script or opens a shop, and for an npc with neither it logs
`"NPC {} ({}) is not coded"` and returns. Quest dialogue lives in
`Quest.nx/Say.img`, so the client is what decides a quest is on offer, plays the
conversation, and reports the outcome with `QUEST_ACTION`.

Everything the client decides is re-checked by the server, which is the only
thing that actually starts or completes a quest. A client that asks for
something it should not get is ignored.

## Files

New:

| File | Role |
|---|---|
| `Data/QuestData.{h,cpp}` | Lazily-loaded `Quest.nx` wrapper; npc→quest index |
| `Character/QuestLog.{h,cpp}` | Quest state store (rewritten from the stub) |
| `Net/Packets/QuestPackets.h` | `QUEST_ACTION` (0x6B) |
| `Net/Handlers/QuestHandlers.{h,cpp}` | `QUEST_CLEAR` (49), `UPDATE_QUEST_INFO` (211) |
| `Gameplay/QuestDialogue.{h,cpp}` | Conversation driver, npc menu, balloon logic |
| `IO/UITypes/UIQuestLog.{h,cpp}` | The quest log window |
| `IO/Components/TextAnalyzer.{h,cpp}` | Port of the client's `CTextAnalyzer` |
| `IO/Components/QuestSummary.{h,cpp}` | Owns and draws one laid-out result |

Touched: `Audio` (QuestClear/QuestAlert sounds), `CharEffect` (QuestClear),
`Inventory` (`get_total_item_count`), `Player` (const `get_quests`),
`Configuration` (`PosQUEST`), `Npc` (`get_id`, quest balloon), `MapNpcs`,
`UIElement` (`QUESTLOG`), `UIStateGame` (Q key), `UINpcTalk` (local dialogue
mode), `MessagingHandlers`, `SetfieldHandlers`, `OutPacket`, `PacketSwitch`.

## Reverse-engineered from GMSv95.exe + MapleStory.pdb

Using the toolchain in `scripts/re/`. Protocol details are in
`ms-network-protocol.md`; the behavioural findings are below.

### NPC balloon — `CNpc::SetQuestList` (0x671980)

Sorts the npc's quests into five arrays; `CNpc::ShowQuestList` (0x672b50) picks
one balloon from `UI/UIWindow2.img/QuestIcon/<n>`.

| Bucket | Filled with | Icon |
|---|---|---|
| `+0xd4` | started, `CheckCompleteDemand` passes | 2 (chest) |
| `+0xcc` | `CheckStartDemand` passes | 0 (lightbulb) |
| `+0xd0` | started, complete demand not met | 1 (plain) |
| `+0xd8` | startable but `IsWorthlessQuest` | 3 |
| none | | 6 (hidden) |

Precedence `0xd4 > 0xcc > 0xd0 > 0xd8`, from the branch chain at `0x671c26`.
Art identities confirmed by rendering the bitmaps, not assumed from ordering.

### NPC quest menu — `CNpc::ShowQuestList` (0x672b50)

When the npc's quest buckets differ it builds a **`CUtilDlgEx` locally** instead
of sending `TALK_TO_NPC`. It lists quests; it does not pick one. It opens the
window with the npc's default talk line read from `template + 0xb8` — in the
data these are `String.nx/Npc.img/<npcid>/d0`, `d1`, …

### Rich-text renderer — `CTextAnalyzer`

The parser is one class, `CTextAnalyzer`, and `AnalyzeText` (0x987cc0) is its
only entry point:

```
long AnalyzeText(ZXString<char> sText, ZArray<CT_INFO>& aCT,
                 ZArray<IWzFont>& aFont, int nBackColor,
                 unsigned short usQuestID, int bList)
```

It has two callers that matter. `CUIQuestInfo::LoadData` (0x832d40) runs it
once per line of `asDesc` and once per line of `asSummary`, with an analyzer
whose **`m_nMargin` is 1 and `m_nWidth` is 255**; `CUtilDlgEx::SetUtilDlgEx`
(0x98e9f0) runs it for npc dialogue, with `GetBasicCTMargin` /
`GetBasicCTWidth` giving margin 8 and width 341, or 210 when the dialogue
shows no npc. The return value is the laid-out height.

`CT_INFO` is 68 bytes, read from the PDB rather than inferred:

```
0x00 nType   0x04 nItemNo  0x08 nLine   0x0c pFont   0x10 sText  0x14 pIcon
0x18 nLeft   0x1c nTop     0x20 nWidth  0x24 nHeight 0x28 nSelect
0x2c nUnderLine  0x30 bLineChange  0x34 nFuncCode  0x38 bReward
0x3c nNpcNo  0x40 nMapNo
```

`nType` is 0 text, 1 icon, 2 picture, 3 control code, 4 selection marker.
`CUIQuestInfoDetail::Draw` (0x8248c0) draws only types 0 and 1.

**Phrases.** `GetPhrase` takes one phrase off the front of the string, and the
rule is not "scan to the next hash":

- `\r` or `\` plus the character after it is a line break, which is why quest
  text can spell one as the two characters `\` and `n`.
- `##` is a literal `#`.
- After `#`, the letter decides. For `@ B F L M _ a c f h i m o p q s t u v x
  y z` the argument is read a character at a time and ends at `#`, `\`, `\r`
  or end of string — **the closing `#` is consumed and dropped**. For `D Q R W
  j` everything up to the next `#` is taken in one go. Every other letter
  gives a bare two-character phrase.
- Anything else runs until one of `# \ \r`.

That is what `#h #` means: `h` reads a run, so the phrase is `#h ` with the
space still on it.

**Phrase types.** `GetPhraseType` (0x97d650) returns 1 `#L`, 2 `#E`, 3 `#I`,
4 `#S`, 5 `#K`, 6 `#w`, 7 `#i`/`#v`, 10 `#s`, 11 `#F`/`#f`, 13 `#B`, 14 `#j`,
15 `#Q`, 16 `#D`, 17 `#W`, 18 anything else carrying an argument, 0 plain
text. Types 8, 9 and 12 have no letter that reaches them; they are dead
branches in the client, one of which is the only caller of
`CheckSecretItemID`.

Before the type is asked for, the phrase is compared against `#k #r #g #b #d
#e #n #l` as strings. Those set state and emit nothing: `#k` restores the
default colour, `#r #g #b #d` set red, green, blue and violet, `#e` and `#n`
turn bold on and off, `#l` closes a selection. `#w` toggles `bReward`.

**Font choice** is `aFont[colour * 2 + bold]`, twelve fonts for six colours in
two weights. Colour 0 and colour 4 are both "default"; `nBackColor` picks
which, and `CUIQuestInfo::SetBackFont` swaps the pair so description and
summary panels can differ.

**Argument tokens** (type 18) dispatch on the letter through a second table at
0x98c950/0x98c90c:

| Token | Becomes |
|---|---|
| `#h ` | character name, then a Korean particle when the next character is `1`, `2` or `3` |
| `#t<id>` | item name; sets `nItemNo` only when the phrase carries a `:` |
| `#z<id>` | item name; always sets `nItemNo` |
| `#c<id>` | `CWvsContext::GetItemCount` — how many the character holds |
| `#o<id>` | mob name from `String/Mob.img/<id>/name` |
| `#M<questid>` | name of the mob the character picked for that quest, via `CharacterData::_GetQuestValue` — a `SELECTEDMOB` record the v83 server has no equivalent of |
| `#p<id>` | npc name from `String/Npc.img/<id>/name` |
| `#@<id>` | npc name, and sets `nNpcNo` |
| `#m<id>` | map name, and sets `nMapNo` |
| `#q<id>` | skill name, id formatted `%07d` |
| `#a<n>` | `GetQuestMobCount` — kill progress |
| `#x<n>` | `GetQuestBonusEXP` |
| `#y<n>` | quest name from `Quest/QuestInfo.img/<n>/name` |
| `#u<n>` | quest state: StringPool 4314 `Not Started`, 4315 `In Progress`, 6704 `Complete` |
| `#R<n>` | the quest's own progress record, or the literal `(empty)` |
| any other | nothing |

`#a<n>#` encodes `questid * 10 + (mobIndex + 1)`. Verified against **all 121
uses: 121 matches, 0 mismatches**, which is what makes it safe to decode.
`GetQuestMobCount` reads the counter out of the progress string three digits
per mob and formats it with StringPool 6678, `%d / %d`.

`#i`/`#v` take the item icon at its own size. `#W` resolves to
`UI/UIWindow2.img/Quest/quest_info/summary_icon/%s` — `basic` (given
outright), `prob` (rolled for), `select` (chosen from). `#f`/`#F` is a canvas
at a full wz path. Only `#s` goes through `GetOutlineCanvas`, which centres
the icon on a 32×32 canvas over `UI/UIWindow.img/IconBase/0`.

**Layout** is two further passes over the pieces.

Across a line, a piece fits when `x + nWidth + m_nMargin <= m_nWidth`, and `x`
starts each line at `m_nMargin`. A picture too wide to ever fit is placed
anyway instead of being pushed onto a line it would not fit either. Text that
does not fit is broken: the font is asked how much fits in
`m_nWidth - x - m_nMargin - 10`, and the answer is then walked back to a
space, unless the piece starts the line and there is no space to walk back to.

If a line would *begin* with a fragment — `IsSuffix` recognises `s`, `es`,
`'…`, and `! , - . ?` when a space or the end follows — the break is moved
back into the piece before it, at the earliest of that piece's last space,
last `-` and last `(` (`IsDilimiter`). With no such place to break, the whole
preceding piece comes down instead.

Down the page, `GetLine` collects each line's first and last piece and its
tallest, with **16 as the floor**. `AdjustHeight` then sets

```
nTop = y + lineHeight - nHeight + (nSelect != -1 ? 10 : 0)
nUnderLine = nHeight + 1
```

so pieces hang from a **baseline, not a centre line** — which is what keeps an
item icon and the words beside it sitting on the same line. `y` starts at
`m_nMargin` and advances by `lineHeight + 2`; the total is `m_nMargin + y`.

### Quest list grouping — region categories

Each tab groups its quests by region, under a collapsible header.

The group is `Quest/QuestInfo.img/<id>/area`, read into `QuestInfo::nCategory`
by `CUIQuestInfo::InsertQuestInfo` and returned by
`CQuestMan::GetQuestCategory`. `sortkey` orders the quests inside a group and
`m_mCountInCategory` counts them.

The group **names** are `CQuestMan::m_asQuestCategoryName`, filled at 0x6c3df0
by walking `Etc/QuestCategory.img` with `_itow(n)` as the key and appending
each entry to the array in order. So `area` is a plain index into that image.
`CUIQuestInfo::GetCategoryOf` clamps anything at or past the end back to 0,
and `CUIQuestInfo::Draw` labels a header with
`CQuestMan::GetQuestCategoryName(area)`.

In v95 each `QuestCategory.img/<n>` is the name itself, a bare string, and `n`
is what a quest's `area` states. In this project's data the entries have been
renumbered and grown into records: the original number is kept in `category`,
the name has moved to `title`, and `areaN` lists the map id prefixes that
belong to the region. **So the match here is on `category`, not on the entry's
name.**

That is checked against the quests, not assumed. Matching on the entry name
files Luminous skill quests under "Hero From the Past, Aran", the Nautilus
quests under "Ellinia", and Mihile's advancement under "The Dragon Master,
Evan"; matching on `category` puts all three where they belong. Eight such
cases were tried and `category` is right in all eight.

96 regions come out of it — `Job`, `Zero`, … `Maple Island`,
`Victoria Island`, `Henesys`, `Ellinia`, `Perion`, `Kerning City`, …
`Masteria`. Because `area` **is** the reference client's array index,
counting up through the numbers is the order it lists regions in.

**The list's own geometry** is stated by
`CUIQuestInfo::GetQuestIdxFromMousePos`:

```
rect = { left 14, top 52, right 216,
         bottom (count - scrollpos) * 22 + 52 }
row  = (mouseY - 52) / 22 + scrollpos
```

Rows begin at y 52, step 22, run x 14 to 216, and `SetScrollBar` scrolls to
`count - 13`, so thirteen are on screen; the scrollbar's `CreateCtrl`
(`CWnd*, uint, long × 5, void*`) puts it at x 217, y 48, length 318, starting
exactly where the rows stop.

**None of those absolute numbers transfer**, because this data does not ship
v95's window. `UIWindow2.img/Quest/list` here is a later revision of the same
node carrying a search box, level and location filters and a recommended
section — `BtSearch`, `BtAllLevel`, `BtAllLocation`, `recommend` — and its
strips `searchArea`, `recommendTitle` and `completeCount` are all **275**
wide, against v95's 202-wide row. So the window's own measurements stand, and
only the proportions of a row's contents are taken from the client.

**A region heading** is drawn by `CUIQuestInfo::Draw` as three pieces:

| Piece | Where |
|---|---|
| Filled band | (11, row + 0), 202 × 19, ARGB `0xFF737573` |
| `BtMin` / `BtMax` picture, 13 × 12 | (14, row + 3) |
| Name, `get_basic_font(22)` | (31, row + 4) |

`get_basic_font(22)` is `IWzFont::Create("Arial", 11, 0xFFFFFFFF, …)`: of its
two integer arguments one is plainly the size, since it is 11 here and 12 for
`get_basic_font(0)`, and the other is `0xFFFFFFFF` — opaque white in the same
ARGB form as the band's `0xFF737573`. Nothing sets a colour on the font
afterwards and `DrawTextA` is handed only `vtMissing`, so white is what the
font carries.

The caption is `"%s (%d)"` — the region's name and how many quests it holds,
straight out of `m_mCountInCategory`, so the count beside the name is the
client's, not a choice. `BtMin` shows while the region is open, `BtMax` while
it is shut.

A collapsed group is remembered per tab as the option id
`3000 + tab + area * 4` while open and `4000 + tab + area * 4` while shut
(`ToggleCategory`, `IsMinimizedCategory`); `InsertQuestInfo` then simply omits
that group's quests from the list.

### NPC quest menu — `CNpc::ShowQuestList` (0x672b50)

The menu is one `CUtilDlgEx` built from a single string: the npc's default
line, then its quests sorted into the same buckets the head balloon uses,
each bucket headed by a **picture**, not a caption. The strings are the
client's own:

| StringPool | Contents | Heads |
|---|---|---|
| 6591 | `\r\n\r\n#fUI/UIWindow2.img/UtilDlgEx/list3#\r\n` | quests ready to hand in (`+0xd4`) |
| 6589 | `\r\n\r\n#fUI/UIWindow2.img/UtilDlgEx/list1#\r\n` | quests available to start (`+0xcc`) |
| 6590 | `\r\n\r\n#fUI/UIWindow2.img/UtilDlgEx/list0#\r\n` | quests under way (`+0xd0`) |
| 6637 | `\r\n\r\n#f…/QuestGuide/HighLVQuestMark/forTalkWindow/0#\r\n` | quests below the player's level (`+0xd8`) |
| 6592 | `\r\n\r\n#fUI/UIWindow2.img/UtilDlgEx/list2#\r\n` | the npc's script entries, from `Etc/ScriptInfo.img` |

Each quest is one line, StringPool 3236: `#d#L%d# %s#l#k`.

So the separator is a `#f` picture token, which the rich-text renderer
already draws — the reason it never appeared is that this client's dialogue
sent selection pages down a plain-text path that strips markup.

### Quest giver and title — `CUIQuestInfoDetail::SetNPC` (0x82fd70)

The quest's giver is shown in the detail pane. `SetNPC` takes the npc from
`CQuestMan::GetStartDemand(questid)->dwNpcTemplateID`, makes a layer and
`RelMove`s it to **(231, 110)** — the npc hangs from its own origin, at its
feet — then animates it with `GA_REPEAT`.

What it puts in the layer is a canvas at `Npc/%07d.img/info/default` when one
exists, and otherwise the npc's own frames through
`CActionMan::LoadNpcAction`. No npc in this data carries `info/default`, so it
is always the second path, which is the same `stand` animation the map draws.

The title beside it is drawn at (35, 42), and `Draw` runs it through
`format_string` with a width of **145** first — so a long name is cut to that
rather than wrapped.

### The effect on the character

Completing a quest plays `Effect/BasicEff.img/QuestClear` — eleven frames —
over the character, with the `QuestClear` sound behind it.

`CUser::OnEffect` dispatches the user-effect packet on a leading byte through
a table at 0x8fb434, and **case 11** is the one that runs it, attaching the
animation to the user's own position vector. `QUEST_CLEAR` (49) plays the same
pair.

Cosmic sends neither for an ordinary hand-in: `Quest.complete` announces only
a record update, and `getShowQuestCompletion` is called from map scripts
alone. So the effect is fired here off the record turning `COMPLETED`, which
is the moment the server confirms the hand-in. Case 11 is wired up as well,
for a server that does send it.

There is no separate effect for starting a quest or for progress. The
`QuestAlert` art belongs to the auto-quest icon
(`CWvsContext::GetAutoQuestIconUOL`), not to the character, and
`Effect/BasicEff.img/QuestEffect/%s` is the per-quest `showEffect` — which 25
of this data's 10675 quests state, and which `CUserLocal::ViewAutoQuestAlert`
plays.

### Quest tracker — `CUIQuestAlarm`

The tracker exists and its art ships with this data, all 180 wide, matching
the window `Create` asks for:

```
UI/UIWindow2.img/QuestAlarm/
  backgrndmax    180x25   title bar, while open
  backgrndmin    180x20   the whole window, while shut
  backgrndcenter 180x18   one row
  backgrndbottom 180x5    footer
  BtMax BtMin BtAuto BtQ BtDelete
```

`Create` is `CreateUIWndPosSaved(180, GetHeight() + 30, 10)` — 10 being the
saved-position slot. `GetHeight` returns **−10** when shut or empty, so the
window is exactly `backgrndmin`; otherwise it counts **18 pixels a row**:
one row for the quest's name, one per demand item, one per demand mob, one if
it wants meso, one per prerequisite quest, and one blank between quests, less
the trailing blank. 25 + rows × 18 + 5 lands on the same number.

`RegisterQuest(questid, bAuto)` refuses in order: an automatic call while
auto-register is off; category 51; a quest with no complete-demand; a quest
demanding no item, no mob, no meso and no prerequisite; a quest already
listed; an automatic call for a quest the player has deleted before
(`m_aDeletedQuestID`); and **a sixth quest — five is the limit**. A manual
add goes into the first slot with no update time, capped at index 4; an
automatic one appends and records `timeGetTime()`. Either way
`CConfig::AddQuestAlarm` persists it, the window is rebuilt, and it opens
itself if it was shut.

`GetProgressFont(percent)` picks the colour: red under 33, red-violet under
66, orange under 100, green at 100. `OnCreate` makes seven fonts, all 11px
Arial, and the ARGB each is created with is what those names mean:

| Member | `FONT_TYPE` | Colour |
|---|---|---|
| `m_pFont` | 22 | `0xFFFFFFFF` white |
| `m_pFontTitle` | 23 | `0xFFFFFFFF` white |
| `m_pFontTitleSkyBlue` | 31 | `0xFF77CCFF` |
| `m_pFontRed` | 34 | `0xFFFF2020` |
| `m_pFontRedViolet` | 38 | `0xFFFF3399` |
| `m_pFontOrange` | 40 | `0xFFFF9900` |
| `m_pFontGreen` | 46 | `0xFF28C99B` |

So the labels are white and the four progress colours are brighter than the
palette's nearest equivalents — the last is a mint rather than a green, and
the second a magenta rather than a violet.

`OnButtonClicked` gives every control a number, and those numbers are the
window's whole behaviour:

| Id | Control | Does |
|---|---|---|
| 1000 | close | empties the list, `DeleteAllQuestAlarm`, hides the window |
| 2000 | `BtMax` | opens it, or says so in chat when nothing is tracked |
| 0x7d1 | `BtMin` | shuts it |
| 0x7d2–0x7d6 | `BtDelete`, one per quest | remembers the quest in `m_aDeletedQuestID`, says so in chat, then `DeleteQuestByIndex` |
| 0x7d7 | `BtAuto` | toggles auto-register and saves it |
| 0x7d8 | `BtQ` | toggles the quest window |

`Draw` blits exactly twice: `backgrndmax` at (0, 0), and `backgrndcenter` at
(0, `25 + 18 * row`) for each row — the same 25 and 18 `GetHeight` counts in.
The window's title is StringPool 3660, **`Quest Helper (%d/5)`**, formatted
with how many quests are on the list and drawn once at (19, 5) with the white
font, before the code branches on whether the window is open. Every row's
label goes at **x 10**, name and demand alike — there is no indent — and its
figure against the right margin.

A quest's delete button sits at `CalcTextWidth(name) + 15` on its own name
row, and its art carries no origin because `Draw` places it.

The other four place **themselves**. `CLayoutMan::AddButton` builds a
`CCtrlOriginButton`, which takes its spot from the art's `origin` — negated —
which is why that call carries no coordinates to find:

| Button | `origin` | Sits at |
|---|---|---|
| `BtQ` | (−4, −4) | (4, 4) |
| `BtAuto` | (−118, −4) | (118, 4) |
| `BtMin`, `BtMax` | (−150, −4) | (150, 4) |

`BtQ` and `BtAuto` agree exactly with the coordinates their `CreateCtrl` calls
pass in `OnCreate`, which is what confirms the origin is placement rather than
a drawing offset — and gives `BtMin`/`BtMax`, whose call could not be decoded.

`origin` is a **vector** node. Reading it as `origin["x"]` returns zero for
every graphic in the file and quietly loses all of this.

The client's text y is the top of an 11 pixel cell, which is shorter than the
box a `Text` measures here, so a y copied across lands low. Text is placed by
centring it in the band it belongs to, which is where the client's cell ends
up in both the 25 pixel title bar and an 18 pixel row.

**Following a quest by hand** is `CUIQuestInfoDetail`'s `m_pBtRegisterAlarm`,
drawn from `UI/UIWindow2.img/Quest/quest_info/BtArlim`.

`SetButton` makes two different virtual calls per button, and the difference
matters: **+0x24 shows or hides, +0x1c enables or disables**. It opens by
hiding every button and enabling it, then shows the ones the quest's state
calls for.

For a quest that is under way or already done it **shows** Quest Helper, and
**disables** it — leaving it on screen, greyed — when the quest asks for
nothing countable, when the alarm list already holds five, or when the list
already holds this quest. It never takes the button away. Forfeit is shown for
both states too, and disabled once the quest is done or its id falls in
1200–1399.

Every button in the pane places itself from its art's origin, the same way the
tracker's do:

| Button | `origin` | Sits at |
|---|---|---|
| `BtAccept` / `BtFinish` | (−11, −371) | (11, 371) |
| `BtNavi` | (−84, −371) | (84, 371) |
| `BtArlim` | (−151, −371) | (151, 371) |
| `BtNPC` | (−206, −371) | (206, 371) |
| `BtGiveup` | (−226, −371) | (226, 371) |
| `BtClose` | (−278, −6) | (278, 6) |

`Draw` (0x82d850) keeps the row cursor in one place: it starts at **25**,
immediately under the title bar, and every row adds **18**. A row's label is
drawn at **x 10** and its progress figure ends at **175**, which is the
window's right margin (the client reaches it as `0xa0 + 15`). The window's
own title sits at (19, 5).

Built as `IO/UITypes/UIQuestTracker.{h,cpp}`, with `BtArlim` wired into the
quest window's detail pane under the same three conditions. Left out, and
worth doing when someone needs them: the per-row `BtQ` and `BtDelete` buttons
that `Draw` creates, `CConfig`'s `LoadQuestAlarm`/`SaveQuestAlarm` so the list
and the auto-register setting survive a relog, `CheckSecretItem`, and
`GetProgressFontMeso`, which colours a meso figure on a scale of its own.
What `m_pFontTitleSkyBlue` marks is also still unread.

### NPC dialogue behaviour — `CUtilDlgEx`

**A page arrives a character at a time.** `CUtilDlgEx::Update` (0x97b3c0)
steps `m_nCurDisplayTextItemPos` on once a frame while `!m_bNoNPC &&
!m_bFinishShow` and invalidates the window. `Draw` then shows every `CT_INFO`
before `m_nCurDisplayItemIndex` whole and `Left(text, m_nCurDisplayTextItemPos)`
of the one at it; when that count runs past the piece's length it moves the
index on and resets the count, so a piece that is not text shows whole and
costs one frame. `m_bFinishShow` is set once the index reaches the piece
count. It only animates while an npc is on screen — `m_bNoNPC` skips it.

**Hover is recomputed from nothing on every move.** `CheckMousePoint`
(0x97e5d0) starts at -1, walks the pieces, and keeps the `nSelect` of any
piece under the cursor; at the end it assigns that to `m_nSelect`, so moving
off an option clears it rather than leaving it lit. A piece counts as under
the cursor within its own rectangle **plus eight pixels** to the right, and
only once it has been revealed — `index <= m_nCurDisplayItemIndex` — unless it
is a selection, which can be pointed at straight away.

**Options act on release, not on press.** `OnMouseButton` (0x982f30) records
the piece under the cursor on `WM_LBUTTONDOWN` (0x201); on `WM_LBUTTONUP`
(0x202) it runs `CheckMousePoint` again and does nothing unless the piece
under the cursor is still the one the press landed on. That is the same rule
the quest list's rows follow.

**Ending a conversation sends nothing.** `CNpc::ShowQuestList` sends
`TALK_TO_NPC` (opcode 0x3f, then the npc's oid and the player's x and y)
**once, before** it opens its local menu — never when the menu is dismissed.

### Quest window geometry — `CUIQuestInfoDetail`

The detail pane is `CreateWnd(296, 396)`. Everything in it comes off `Draw`
(0x8248c0) and `SetLayout` (0x823020):

| Piece | Where |
|---|---|
| Quest name | (35, 42) |
| "Worthless quest" line | (35, 56) |
| Level limit | (23, 75) |
| Description text | x 17, y 127 − scroll |
| Description clip | (18, 128), 253 wide, `GetScrLogLen() − 2` tall |
| Description scrollbar | x 275, y 127, wheel range 263 |
| Reward box text | x 17, y 257 − scroll |
| Reward box clip | (18, 252), 253 × 111 |
| Reward box scrollbar | x 275, y 251, length 113 |

`GetScrLogLen` is **120** when the quest states a reward box and **238** when
it does not, less 15 again for a delivery or timed quest. The reward box
itself is only drawn in the first case, which is why the description gets the
rest of the pane in the second.

Both panels **clip and scroll**, and the client sets a clip rectangle around
each before drawing into it. The demand and reward blurbs are consecutive
entries of the quest's summary and are stacked by their own heights, with
nothing added between them.

### Notification wording

StringPool 786 = `New Quest!`, 787 = `Quest Complete!`.

## Bugs found and fixed

| Bug | Cause |
|---|---|
| Started-quest list mis-parsed | The list's length counts more entries than quests: a quest tracking progress in another's record is sent as two pairs. Resolved by asking whether the previous quest named the following id as its `infoNumber`. |
| Info records shown as started quests | `updateQuest(..., infoUpdate=true)` hardcodes status byte **1**, so they arrive looking identical to real quests. Both paths now go through one ordered entry point. |
| Wrong window art | Built against `UIWindow.img/Quest` (pre-Big-Bang) with hand-guessed coordinates. `UIWindow2.img/Quest` is correct, and every piece carries the `origin` it belongs at. |
| Garbage text (`3arn 50 points`) | `replace_macros` did not know `#L…#l`, so a link's closing `#` paired with the option's first letter — eaten as a formatting token when it was one of `b d e f g k n r`. The corrupted `#L0#` then failed the option parser, collapsing the list into one paragraph. |
| Digits leaking (`4000018:#`) | The token parser required digits followed immediately by `#`, but the data spells items both `#t4000018#` and `#t4000018:#`. |
| `t4000004` in descriptions | Descriptions still went through a naive "skip to next `#`" scanner that desynchronises on strings like `#b#c4000003##k/40`. |
| No icons ever rendered | Inline icons were capped at 20px; real item icons are ~27×31, so every one failed the check. |
| Icons out of line with text | Text runs were created with a wrap width, so a run wrapped *inside* one piece — several lines tall but positioned as one. Layout is now word-by-word, one line per piece. |
| Literal `\n` on screen | Some strings store line breaks as the two characters `\` + `n` (e.g. quest 2091). |
| Pieces centred on the line | Guessed while the renderer was still unlocated. The client hangs them from a baseline, so an item icon and its name were half a line out of register. |
| Lines could collapse to nothing | No minimum line height. The client floors every line at 16. |
| Icons spaced by an invented 2px | The client advances by the piece's own width and nothing else. |
| Colour tokens half wrong | `#g` was drawn as dark grey rather than green, and `#e`/`#n` were dropped instead of switching to a bold font. |
| Lines starting with `s` or `,` | The client moves that break back into the piece before it; nothing here did. |
| Text bled out of the pane | Two causes. The detail pane never clipped or scrolled, and `AnalyzeText` retries a piece that nothing fit of (0x98c694 jumps back **without** advancing the index) where the port advanced anyway, leaving it at a stale x with its full width. |
| `bluetextnormal text` | `GraphicsGL::drawtext` dropped any space at `ax == 0` without advancing. Runs split by a colour code begin with exactly that space. It now asks the layout whether the space was given room, which is the same question `LayoutBuilder::add` answered when it measured. |
| Quest list scrollbar had no grabbable knob | Two causes, both in the plumbing rather than the geometry. This window handed `Slider::send_cursor` **absolute** coordinates where every other window passes window-relative, so the thumb's hit test never matched and dragging did nothing. And `Slider` itself tested the cursor against a flat eight pixels of width when every scrollbar graphic is **eleven** wide, so the right edge of the thumb was dead even once the coordinates were right. |
| Reward icons out of place | Two causes. `Texture::draw` hangs a bitmap from its WZ origin where the client blits the canvas top-left at the offset it measured; and `GetItemIcon(id, 1, 0)`'s second argument picks the framed `icon`, not the `iconRaw` this was reading. |

## Verified vs. chosen

Verified from the binary or the data: the balloon bucket mapping and
precedence, the `QuestIcon` path, the `#a` encoding, the token set, the
notification strings, the `d0` default lines, the `#L` corruption mechanism,
the literal-`\n` convention, and — since `CTextAnalyzer` was found — the whole
of the rich-text renderer: phrase rules, token meanings, font selection,
fitting, line-breaking and baseline.

Chosen, not read out of the client:

- The balloon's vertical offset. The client anchors via `CAvatar::GetHeight()`,
  which has no direct equivalent here.
- The exact RGB of the twelve fonts. The client builds them with
  `IWzFont::Create`; this uses the nearest of `Text::Color`, with `GREEN`
  added for `#g`.
- Drawing `#f` pictures. `CUIQuestInfoDetail::Draw` renders only types 0 and
  1, so a `#f` in a quest summary is laid out and then skipped; dropping art
  the data asks for seemed the worse of the two, so this draws it.
- The npc dialogue keeps its own margin of 0 and width of 320 rather than the
  client's 8 and 341, because it already indents its body by `DIALOG_TEXT_X`
  where the client keeps the indent inside the layout.

## Open items

- `#B`, `#j`, `#Q` and `#D` are laid out as empty pieces. Their handlers are
  the bulk of `AnalyzeText` (0x988a33, 0x988e9c, 0x98b100, 0x98b397) and none
  of them appears in the v83 quest data.
- `strip_npc_tokens` still pairs hashes positionally, the same structural
  weakness that caused the `#L` bug. It is only reached for non-selection
  pages. Worth unifying onto the single tokenizer.
- `Effect/Quest.img/%d`, which v95 uses for the balloon, **does not exist in
  this v83 data**; `UIWindow2.img/QuestIcon` does and is what `SetQuestList`
  also references.
- Reward-choice selector for quests offering a pick between items:
  `QuestActionPacket::complete(..., selection)` exists but nothing calls it.
- Korean-only strings render as `?????` — a property of the v83 data, not
  something the client can fix.
- `#h` drops the Korean particle the client appends for `#h1`, `#h2` and
  `#h3`. The v83 data only ever writes `#h #`.
- `#M` and `#x` render nothing. Both read per-character quest state — a picked
  mob and a bonus-exp figure — that this server does not send. Neither appears
  in the v83 quest data.
- The detail pane scrolls a text line at a time, where the client scrolls by
  the pixel with a `CCtrlScrollBar`. The window has no pixel-stepped scrollbar
  component to hand.
- Which regions are shut lasts only as long as the session. The reference
  client writes it to its options as `3000 + tab + area * 4` / `4000 + …`;
  this client has nowhere to put that yet.
- 207 quests state an `area` the category file does not name, so they get an
  `Area <n>` heading of their own. The reference client would fold every one
  of them into region 0, `Job`, because `GetCategoryOf` clamps out of range —
  a heading of their own says more, and is easy to change back.
- There is a fourth quest list, `m_aaQuestInfo[3]`, holding every quest whose
  id falls in 1200–1399. What shows it is not yet known.
- Nothing yet reads `CUIQuestInfoDetail`'s remaining parts: the npc portrait,
  the accept and give-up buttons' own layout, the series-quest gauge, the
  timer text, and the mob and map location markers.

## Testing

- **Quest 2000, "Fixing Blackbull's House"** (npc 1020000, Perion, level 10+) —
  the whole loop with no scripts: accept, see it under In Progress, return
  without the items for the `stop/item` line, collect 30 Tree Branches and
  50 Firewood, hand in for 300 EXP and a shield.
- **Quest 1000, "Borrowing Sera's Mirror"** (2101 → 2100, Henesys) — two-npc
  chain, exercises `nextQuest` and the Completed tab.
- **Quest 1021, "Roger's Apple"** — has `startscript`/`endscript`, so it must
  send `QUEST_ACTION` 4/5 and let the **server** draw the dialogue. Client-drawn
  pages there mean the scripted branch is wrong.
- **Quest 10000** — `#L` links and `ask=1`; covers selection and accept/decline.
- **Quest 2000's completed description**, `#b30 tree branches and 50
  firewoods#k` — no icon token in the sentence, so it is a colour change only
  and should read as blue text going back to the panel's own colour.
- **Quest 2091, "I'm Bored 2"** — literal `\n` and interleaved `#c`/`#k`;
  the regression case for the text bugs above.
- **Nella (1052103)** and **Agent E** — multi-quest npcs; the menu should open
  with the npc's `d0` line and list quests beneath it.
- Press **Q** for the log; switch tabs; forfeit an in-progress quest (it should
  leave the list only when the server's reply arrives, not optimistically);
  relog mid-quest to check the field-entry parse round-trips; click a quest npc
  that also has a shop, decline, and confirm the shop still opens.

## Building

`./scripts/build_wasm.sh`, or `./scripts/docker_build_wasm.sh` when the local
Emscripten toolchain is unavailable. The build must be warning-free.
