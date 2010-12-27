// -*- c-basic-offset: 4; related-file-name: "../../lib/xiaheader.cc" -*-
#ifndef CLICK_XIAHEADER_HH
#define CLICK_XIAHEADER_HH
#include <click/string.hh>
#include <click/glue.hh>
#include <clicknet/xia.h>
#include <click/vector.hh>

CLICK_DECLS
class StringAccum;

class XIAHeader { public:

    /** @brief Construct an XIAHeader */
    XIAHeader(size_t dsnode);
    XIAHeader(const struct click_xia& hdr);
    ~XIAHeader();

    const struct click_xia& hdr() const;
    struct click_xia& hdr();
    operator struct click_xia() const;

    size_t size() const;
    static size_t size(uint8_t dsnode);

private:
    struct click_xia* _hdr;
};

inline const struct click_xia&
XIAHeader::hdr() const
{
    return *_hdr;
}

inline struct click_xia&
XIAHeader::hdr()
{
    return *_hdr;
}

inline
XIAHeader::operator struct click_xia() const
{
    return hdr();
}

inline size_t
XIAHeader::size() const
{
    return size(_hdr->dsnode);
}

inline size_t
XIAHeader::size(uint8_t dsnode)
{
    return sizeof(struct click_xia) + sizeof(struct click_xia_xid_node) * dsnode;
}

CLICK_ENDDECLS
#endif