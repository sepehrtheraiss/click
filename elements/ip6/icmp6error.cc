/*
 * icmp6error.{cc,hh} -- element constructs ICMP6 error packets
 * Peilei Fan
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology.
 *
 * This software is being provided by the copyright holders under the GNU
 * General Public License, either version 2 or, at your discretion, any later
 * version. For more information, see the `COPYRIGHT' file in the source
 * distribution.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "click_icmp6.h"
#include "click_ip6.h"
#include "click_ip.h"
#include "icmp6error.hh"
#include "ip6address.hh"
#include "confparse.hh"
#include "error.hh"
#include "glue.hh"

ICMP6Error::ICMP6Error()
{
  add_input();
  add_output();
  _code = _type = -1;
}

ICMP6Error::~ICMP6Error()
{
}

int
ICMP6Error::configure(const Vector<String> &conf, ErrorHandler *errh)
{
  if (cp_va_parse(conf, this, errh,
                  cpIP6Address, "Source IP6 address", &_src_ip,
                  cpInteger, "ICMP6 Type", &_type,
                  cpInteger, "ICMP6 Code", &_code,
		  0) < 0)
    return -1;
  return 0;
}

bool
ICMP6Error::is_error_type(int type)
{
  return(type == ICMP6_DST_UNREACHABLE ||  	// 1
         type == ICMP6_PKT_TOOBIG ||		// 2
         type == ICMP6_TYPE_TIME_EXCEEDED ||	// 3
	 type == ICMP6_PARAMETER_PROBLEM);       // 4

}

int
ICMP6Error::initialize(ErrorHandler *errh)
{
  if (_type < 0 || _code < 0 || (_src_ip == IP6Address()))
    return errh->error("not configured -a");
  if(is_error_type(_type) == false)
    return errh->error("ICMP6 type %d is not an error type", _type);
  return 0;
}

/*
 * Is an IP6 address unicast?
 * check if they are not multicast address (prefix is ff)
 * Can't detect directed broadcast here!
 */

bool
ICMP6Error::unicast(const IP6Address &aa)
{
  const unsigned char *a = aa.data();
  if (a[0]== 0xff)
    return(0);
  return(1);
}


/*
 * Is a source IP6 address valid
 * 
 */

bool
ICMP6Error::valid_source(const IP6Address &aa)
{
  //unsigned int a = aa.s_addr;
  //unsigned int ha = ntohl(a); 
  //unsigned net = (ha >> 24) & 0xff;

  /* broadcast or multicast */
 if(unicast(aa) == 0)
    return(0);

  /* local net or host: */
// if(net == 0)
//    return(0);

  /* I don't know how to detect directed broadcast. */

  /* ::1 */
 if(aa == IP6Address("::1"))
   return(0);

  return(1);
}

/*
 * Does a packet contain a source route option?
 * currently, let's say no option for this
 */

bool
ICMP6Error::has_route_opt(const click_ip6 *ip)
{
  return(0);
}


Packet *
ICMP6Error::simple_action(Packet *p)
{
  WritablePacket *q = 0;
  const click_ip6 *ipp = p->ip6_header();
  click_ip6 *nip;
  struct icmp6_generic *icp;
  unsigned xlen;


  if (!ipp)
    goto out;
  

  /* These "don'ts" are from RFC1885 2.4.e: */

  /* Don't reply to ICMP6 error messages. */
  if(ipp->ip6_nxt == IP_PROTO_ICMP6) {
    icp = (struct icmp6_generic *) ((char *)ipp);
    if( is_error_type(icp->icmp6_type))
      goto out;
  }

  /* Don't respond to packets with IPv6 broadcast destinations. */
  if(unicast(IP6Address(ipp->ip6_dst)) == 0)
    goto out;

  /* Don't respond to e.g. Ethernet broadcasts or multicasts. */
  if(p->mac_broadcast_anno())
    goto out;

  
  /* send back as much of invoding packet as will fit without the ICMPv6 packet exceeding 576 octets , ICMP6 header is 8 octets*/

  xlen = p->length();
  if (xlen > 568)
    xlen = 568;           

  q = Packet::make(sizeof(struct click_ip6) + sizeof(struct icmp6_generic) + xlen);
  // guaranteed that packet data is aligned
  memset(q->data(), '\0', q->length());
  
  //set ip6 header
  nip = (click_ip6 *) q->data();
  nip->ip6_v = 6;
  nip->ip6_pri=0;
  nip->ip6_flow[0]=0;
  nip->ip6_flow[1]=0;
  nip->ip6_flow[2]=0;
  nip->ip6_plen = htons(q->length());
  nip->ip6_nxt = IP_PROTO_ICMP6;  /* next header */ 
  nip->ip6_hlim = 0xff; //what hop limit shoud I set?
  nip->ip6_src = IP6Address(_src_ip.addr());
  nip->ip6_dst = ipp->ip6_src;
  
  //set icmp6 Message
  icp = (struct icmp6_generic *) (nip + 1);
  icp->icmp6_type = _type;
  icp->icmp6_code = _code;

    
if(_type == 2 && _code == 0){
    /* Set the mtu value. */
  ((struct icmp6_pkt_toobig *)icp)->icmp6_mtusize = 1500;
  }
  
  if(_type == 4 && _code == 0){
    /* Set the Parameter Problem pointer. */
    ((struct icmp6_param *) icp)->pointer = p->param_off_anno();
  }
  
  memcpy((void *)(icp + 1), p->data(), xlen);
  icp->icmp6_cksum = htons(in6_fast_cksum(&nip->ip6_src, &nip->ip6_dst, nip->ip6_plen, nip->ip6_nxt, 0, (unsigned char *)icp, sizeof(icmp6_generic)));

  
  q->set_dst_ip6_anno(IP6Address(nip->ip6_dst));
  q->set_fix_ip_src_anno(1);
  q->set_ip6_header(nip, sizeof(click_ip6));

 out:
  p->kill();
  return(q);
}

EXPORT_ELEMENT(ICMP6Error)
