#ifndef RIPPLE_SHAMAP_SHAMAPITEM_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPITEM_H_INCLUDED
#include <ripple/basics/base_uint.h>
#include <ripple/basics/Blob.h>
#include <ripple/basics/Slice.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/beast/utility/Journal.h>
#include <cstddef>
namespace ripple {
class SHAMapItem
{
private:
    uint256    tag_;
    Blob       data_;
public:
    SHAMapItem (uint256 const& tag, Blob const & data);
    SHAMapItem (uint256 const& tag, Serializer const& s);
    SHAMapItem (uint256 const& tag, Serializer&& s);
    Slice slice() const;
    uint256 const& key() const;
    Blob const& peekData() const;
    std::size_t size() const;
    void const* data() const;
};
inline
Slice
SHAMapItem::slice() const
{
    return {data_.data(), data_.size()};
}
inline
std::size_t
SHAMapItem::size() const
{
    return data_.size();
}
inline
void const*
SHAMapItem::data() const
{
    return data_.data();
}
inline
uint256 const&
SHAMapItem::key() const
{
    return tag_;
}
inline
Blob const&
SHAMapItem::peekData() const
{
    return data_;
}
} 
#endif
