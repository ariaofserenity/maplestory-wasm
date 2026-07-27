//////////////////////////////////////////////////////////////////////////////
// NoLifeNx - Part of the NoLifeStory project                               //
// Copyright © 2013 Peter Atashian                                          //
//                                                                          //
// This program is free software: you can redistribute it and/or modify     //
// it under the terms of the GNU Affero General Public License as           //
// published by the Free Software Foundation, either version 3 of the       //
// License, or (at your option) any later version.                          //
//                                                                          //
// This program is distributed in the hope that it will be useful,          //
// but WITHOUT ANY WARRANTY; without even the implied warranty of           //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            //
// GNU Affero General Public License for more details.                      //
//                                                                          //
// You should have received a copy of the GNU Affero General Public License //
// along with this program.  If not, see <http://www.gnu.org/licenses/>.    //
//////////////////////////////////////////////////////////////////////////////

#include "bitmap.hpp"
#include "file_impl.hpp"
#include <lz4.h>
#include <unistd.h>
#include <vector>

namespace nl
{
	bitmap::bitmap(void* f, uint64_t o, uint16_t w, uint16_t h) : m_file(f), m_offset(o), m_width(w), m_height(h)
	{
	}
	bool bitmap::operator<(bitmap const& o) const
	{
		return m_offset < o.m_offset;
	}
	bool bitmap::operator==(bitmap const& o) const
	{
		return m_offset == o.m_offset;
	}
	bitmap::operator bool() const
	{
		return m_file != nullptr;
	}
	std::vector<char> bitmap_buf;
	void const* bitmap::data() const
	{
		if (!m_file)
			return nullptr;
		_file_data* fd = static_cast<_file_data*>(m_file);

		auto const l = length();
		if (l + 0x20 > bitmap_buf.size())
			bitmap_buf.resize(l + 0x20);

		// The compressed length is stored in the four bytes ahead of the data.
		// Taking the uncompressed length instead happens to work whenever
		// compression saves space, but tiny bitmaps grow: a single pixel is four
		// bytes that lz4 stores as five, so the last literal, the alpha byte,
		// was read from past the end of the buffer.
		uint32_t compressed_len = 0;
		if (!io_read(fd, &compressed_len, sizeof(compressed_len), m_offset))
			return nullptr;

		if (compressed_len == 0)
			return nullptr;

		std::vector<char> src_buf(compressed_len);

		if (!io_read(fd, src_buf.data(), compressed_len, m_offset + 4))
			return nullptr;

		// Bounded against both lengths, so a short or damaged block fails rather
		// than reading beyond the input the way the unchecked variant does.
		if (::LZ4_decompress_safe(src_buf.data(), bitmap_buf.data(),
		                          static_cast<int>(compressed_len),
		                          static_cast<int>(l)) < 0)
			return nullptr;

		return bitmap_buf.data();
	}
	uint16_t bitmap::width() const
	{
		return m_width;
	}
	uint16_t bitmap::height() const
	{
		return m_height;
	}
	uint32_t bitmap::length() const
	{
		return 4u * m_width * m_height;
	}
	size_t bitmap::id() const
	{
		return static_cast<size_t>(m_offset);
	}
} // namespace nl
