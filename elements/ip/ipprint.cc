/*
 * ipprint.{cc,hh} -- element prints packet contents to system log
 * Max Poletto, Eddie Kohler
 *
 * Copyright (c) 2000 Mazu Networks, Inc.
 * Copyright (c) 2005 Regents of the University of California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "ipprint.hh"
#include <click/glue.hh>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include <click/packet_anno.hh>
#include <click/router.hh>

#include <clicknet/ip.h>
#include <clicknet/icmp.h>
#include <clicknet/tcp.h>
#include <clicknet/udp.h>

#if CLICK_USERLEVEL
# include <stdio.h>
#endif

CLICK_DECLS

IPPrint::IPPrint()
  : Element(1, 1)
{
#if CLICK_USERLEVEL
  _outfile = 0;
#endif
}

IPPrint::~IPPrint()
{
}

int
IPPrint::configure(Vector<String> &conf, ErrorHandler *errh)
{
  _bytes = 1500;
  String contents = "no";
  String payload = "no";
  _label = "";
  _swap = false;
  _payload = false;
  bool print_id = false;
  bool print_time = true;
  bool print_paint = false;
  bool print_tos = false;
  bool print_ttl = false;
  bool print_len = false;
  bool print_aggregate = false;
  String channel;
  
  if (cp_va_parse(conf, this, errh,
		  cpOptional,
		  cpString, "label", &_label,
		  cpKeywords,
		  "CONTENTS", cpWord, "print packet contents (false/hex/ascii)", &contents,
		  "PAYLOAD", cpWord, "print packet payload (false/hex/ascii)", &payload,
		  "NBYTES", cpInteger, "max data bytes to print", &_bytes,
		  "ID", cpBool, "print IP ID?", &print_id,
		  "TIMESTAMP", cpBool, "print packet timestamps?", &print_time,
		  "PAINT", cpBool, "print paint?", &print_paint,
		  "TOS", cpBool, "print IP TOS?", &print_tos,
		  "TTL", cpBool, "print IP TTL?", &print_ttl,
		  "SWAP", cpBool, "swap ICMP values when printing?", &_swap,
		  "LENGTH", cpBool, "print IP length?", &print_len,
		  "AGGREGATE", cpBool, "print aggregate annotation?", &print_aggregate,
#if CLICK_USERLEVEL
		  "OUTFILE", cpFilename, "output filename", &_outfilename,
#endif
		  "CHANNEL", cpWord, "output channel", &channel,
		  cpEnd) < 0)
    return -1;

  contents = contents.upper();
  if (contents == "NO" || contents == "FALSE")
    _contents = 0;
  else if (contents == "YES" || contents == "TRUE" || contents == "HEX")
    _contents = 1;
  else if (contents == "ASCII")
    _contents = 2;
  else
    return errh->error("bad contents value `%s'; should be `false', `hex', or `ascii'", contents.cc());

  int payloadv;
  payload = payload.upper();
  if (payload == "NO" || payload == "FALSE")
    payloadv = 0;
  else if (payload == "YES" || payload == "TRUE" || payload == "HEX")
    payloadv = 1;
  else if (payload == "ASCII")
    payloadv = 2;
  else
    return errh->error("bad payload value `%s'; should be `false', `hex', or `ascii'", contents.cc());

  if (payloadv > 0 && _contents > 0)
    return errh->error("specify at most one of PAYLOAD and CONTENTS");
  else if (payloadv > 0)
    _contents = payloadv, _payload = true;
  
  _print_id = print_id;
  _print_timestamp = print_time;
  _print_paint = print_paint;
  _print_tos = print_tos;
  _print_ttl = print_ttl;
  _print_len = print_len;
  _print_aggregate = print_aggregate;
  _errh = router()->chatter_channel(channel);
  return 0;
}

int
IPPrint::initialize(ErrorHandler *errh)
{
#if CLICK_USERLEVEL
  if (_outfilename) {
    _outfile = fopen(_outfilename.c_str(), "wb");
    if (!_outfile)
      return errh->error("%s: %s", _outfilename.c_str(), strerror(errno));
  }
#else
  (void) errh;
#endif
  return 0;
}

void
IPPrint::cleanup(CleanupStage)
{
#if CLICK_USERLEVEL
    if (_outfile)
	fclose(_outfile);
    _outfile = 0;
#endif
}

void
IPPrint::tcp_line(StringAccum &sa, const Packet *p, int transport_length) const
{
    const click_ip *iph = p->ip_header();
    const click_tcp *tcph = p->tcp_header();
    int ip_len, seqlen;
    uint32_t seq;

    if (transport_length < 4 || !IP_FIRSTFRAG(iph)) {
	sa << IPAddress(iph->ip_src) << " > " << IPAddress(iph->ip_dst) << ": "
	   << (transport_length < 4 ? "truncated-tcp" : "tcp");
	return;
    }

    sa << IPAddress(iph->ip_src) << '.' << ntohs(tcph->th_sport) << " > "
       << IPAddress(iph->ip_dst) << '.' << ntohs(tcph->th_dport) << ": ";
    
    if (transport_length < 14)
	goto truncated_tcp;

    ip_len = ntohs(iph->ip_len);
    seqlen = ip_len - (tcph->th_off << 2);
    if (tcph->th_flags & TH_SYN)
	sa << 'S', seqlen++;
    if (tcph->th_flags & TH_FIN)
	sa << 'F', seqlen++;
    if (tcph->th_flags & TH_RST)
	sa << 'R';
    if (tcph->th_flags & TH_PUSH)
	sa << 'P';
    if (!(tcph->th_flags & (TH_SYN | TH_FIN | TH_RST | TH_PUSH)))
	sa << '.';
    
    seq = ntohl(tcph->th_seq);
    sa << ' ' << seq << ':' << (seq + seqlen)
       << '(' << seqlen << ',' << p->length() << ',' << ip_len << ')';
    if (tcph->th_flags & TH_ACK)
	sa << " ack " << ntohl(tcph->th_ack);

    if (transport_length < 16)
	goto truncated_tcp;
    
    sa << " win " << ntohs(tcph->th_win);
    return;

  truncated_tcp:
    sa << "truncated-tcp";
}

void
IPPrint::udp_line(StringAccum &sa, const Packet *p, int transport_length) const
{
    const click_ip *iph = p->ip_header();
    const click_udp *udph = p->udp_header();

    if (transport_length < 4 || !IP_FIRSTFRAG(iph)) {
	sa << IPAddress(iph->ip_src) << " > " << IPAddress(iph->ip_dst) << ": "
	   << (transport_length < 4 ? "truncated-udp" : "udp");
	return;
    }

    sa << IPAddress(iph->ip_src) << '.' << ntohs(udph->uh_sport) << " > "
       << IPAddress(iph->ip_dst) << '.' << ntohs(udph->uh_dport) << ": ";

    if (transport_length < 8)
	goto truncated_udp;

    sa << "udp " << ntohs(udph->uh_ulen);
    return;

  truncated_udp:
    sa << "truncated-udp";
}

static String
unparse_proto(int ip_p, bool prepend)
{
    if (ip_p == IP_PROTO_TCP)
	return String::stable_string("tcp", 3);
    else if (ip_p == IP_PROTO_UDP)
	return String::stable_string("udp", 3);
    else if (ip_p == IP_PROTO_ICMP)
	return String::stable_string("icmp", 4);
    else if (prepend)
	return String::stable_string("protocol ", 9) + String(ip_p);
    else
	return String(ip_p);
}

static const char * const icmp_unreachable_messages[] = {
    "net",
    "host",
    "protocol",
    "port",
    "needfrag",
    "srcroutefail",
    "netunknown",
    "hostunknown",
    "isolated",
    "netprohibited",
    "hostprohibited",
    "nettos",
    "hosttos",
    "filterprohibited",
    "hostprecedence",
    "precedencecutoff"
};

void
IPPrint::icmp_line(StringAccum &sa, const Packet *p, int transport_length) const
{
    const click_ip *iph = p->ip_header();
    const click_icmp *icmph = p->icmp_header();

    sa << IPAddress(iph->ip_src) << " > " << IPAddress(iph->ip_dst) << ": ";
    if (transport_length < 2)
	goto truncated_icmp;

    switch (icmph->icmp_type) {
	
      case ICMP_ECHOREPLY:
	sa << "icmp echo reply ";
	goto icmp_echo;
      case ICMP_ECHO:
	sa << "icmp echo request ";
	/* fallthru */
      icmp_echo: {
	    if (transport_length < 8)
		goto truncated_icmp;
	    const click_icmp_sequenced *seqh = reinterpret_cast<const click_icmp_sequenced *>(icmph);
#define swapit(x) (_swap ? ((((x) & 0xff) << 8) | ((x) >> 8)) : (x))
	    sa << '(' << swapit(seqh->icmp_identifier) << ", " << swapit(seqh->icmp_sequence) << ')';
	    break;
	}
	
      case ICMP_UNREACH: {
	  const click_ip *eiph = reinterpret_cast<const click_ip *>(icmph + 1);
	  int eiph_len = transport_length - sizeof(click_icmp);
	  if (eiph_len < (int) sizeof(click_ip)) {
	      sa << "icmp unreachable ";
	      goto truncated_icmp;
	  }
	  const click_udp *eudph = reinterpret_cast<const click_udp *>(reinterpret_cast<const uint8_t *>(eiph) + (eiph->ip_hl << 2));
	  int eudph_len = eiph_len - (eiph->ip_hl << 2);

	  sa << "icmp " << IPAddress(eiph->ip_dst);
	  switch (icmph->icmp_code) {
	    case ICMP_UNREACH_PROTOCOL:
	      sa << " protocol " << unparse_proto(eiph->ip_p, false) << " unreachable";
	      break;
	    case ICMP_UNREACH_PORT:
	      sa << ' ' << unparse_proto(eiph->ip_p, true) << " port ";
	      if (eudph_len < 4) {
		  sa << "unreachable ";
		  goto truncated_icmp;
	      } else
		  sa << ntohs(eudph->uh_dport) << " unreachable";
	      break;
	    case ICMP_UNREACH_NEEDFRAG: {
		const click_icmp_needfrag *nfh = reinterpret_cast<const click_icmp_needfrag *>(icmph);
		sa << " unreachable - need to frag";
		if (nfh->icmp_nextmtu)
		    sa << " (mtu " << ntohs(nfh->icmp_nextmtu) << ')';
		break;
	    }
	    default:
	      if (icmph->icmp_code < 16)
		  sa << icmp_unreachable_messages[icmph->icmp_code];
	      else
		  sa << "unreachable #" << (int)icmph->icmp_code;
	      break;
	  }
	  break;
      }

      default:
	sa << "icmp type " << (int)icmph->icmp_type << " code " << (int)icmph->icmp_code;
	break;
    }
    return;

  truncated_icmp:
    sa << "truncated-icmp";
}

Packet *
IPPrint::simple_action(Packet *p)
{
    const click_ip *iph = p->ip_header();
    if (!iph)
	return p;

    StringAccum sa;

    if (_label)
	sa << _label << ": ";
    if (_print_timestamp)
	sa << p->timestamp_anno() << ": ";
    if (_print_aggregate)
	sa << '#' << AGGREGATE_ANNO(p);
    if (_print_paint)
	sa << (_print_aggregate ? "." : "paint ") << (int)PAINT_ANNO(p);
    if (_print_aggregate || _print_paint)
	sa << ": ";

    if (p->network_length() < (int) sizeof(click_ip))
	sa << "truncated-ip";
    else {
	int ip_len = ntohs(iph->ip_len);
	int payload_len = ip_len - (iph->ip_hl << 2);
	int transport_length = p->transport_length();
	if (p->end_data() > p->network_header() + ip_len)
	    transport_length = p->end_data() - (p->network_header() + ip_len);

	if (_print_id)
	    sa << "id " << ntohs(iph->ip_id) << ' ';
	if (_print_ttl)
	    sa << "ttl " << (int)iph->ip_ttl << ' ';
	if (_print_tos)
	    sa << "tos " << (int)iph->ip_tos << ' ';
	if (_print_len)
	    sa << "length " << ip_len << ' ';

	if (iph->ip_p == IP_PROTO_TCP)
	    tcp_line(sa, p, transport_length);
	else if (iph->ip_p == IP_PROTO_UDP)
	    udp_line(sa, p, transport_length);
	else if (iph->ip_p == IP_PROTO_ICMP)
	    icmp_line(sa, p, transport_length);
	else
	    sa << IPAddress(iph->ip_src) << " > " << IPAddress(iph->ip_dst) << ": ip-proto-" << (int)iph->ip_p;

	// print fragment info
	if (IP_ISFRAG(iph))
	    sa << " (frag " << ntohs(iph->ip_id) << ':' << payload_len << '@'
	       << ((ntohs(iph->ip_off) & IP_OFFMASK) << 3)
	       << ((iph->ip_off & htons(IP_MF)) ? "+" : "") << ')';

	// print payload
	if (_contents > 0) {
	    int amt = 3*_bytes + (_bytes/4+1) + 3*(_bytes/24+1) + 1;
	    
	    char *buf = sa.reserve(amt);
	    char *orig_buf = buf;
	    
	    const uint8_t *data;
	    if (_payload) {
		if (IP_FIRSTFRAG(iph) && iph->ip_p == IP_PROTO_TCP)
		    data = p->transport_header() + (p->tcp_header()->th_off << 2);
		else if (IP_FIRSTFRAG(iph) && iph->ip_p == IP_PROTO_UDP)
		    data = p->transport_header() + sizeof(click_udp);
		else
		    data = p->transport_header();
	    } else
		data = p->data();

	    if (buf && _contents == 1) {
		for (unsigned i = 0; i < _bytes && data < p->end_data(); i++, data++) {
		    if ((i % 24) == 0) {
			*buf++ = '\n'; *buf++ = ' '; *buf++ = ' ';
		    } else if ((i % 4) == 0)
			*buf++ = ' ';
		    sprintf(buf, "%02x", *data & 0xff);
		    buf += 2;
		}
	    } else if (buf && _contents == 2) {
		for (unsigned i = 0; i < _bytes && data < p->end_data(); i++, data++) {
		    if ((i % 48) == 0) {
			*buf++ = '\n'; *buf++ = ' '; *buf++ = ' ';
		    } else if ((i % 8) == 0)
			*buf++ = ' ';
		    if (*data < 32 || *data > 126)
			*buf++ = '.';
		    else
			*buf++ = *data;
		}
	    }

	    if (orig_buf) {
		assert(buf <= orig_buf + amt);
		sa.forward(buf - orig_buf);
	    }
	}
    }

#if CLICK_USERLEVEL
    if (_outfile) {
	sa << '\n';
	fwrite(sa.data(), 1, sa.length(), _outfile);
    } else
#endif
	_errh->message("%s", sa.c_str());

    return p;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPPrint)
