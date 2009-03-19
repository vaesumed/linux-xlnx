#ifndef _TRACE_SKB_H_
#define _TRACE_SKB_H_

DECLARE_TRACE(kfree_skb,
	TP_PROTO(struct sk_buff *skb, void *location),
	TP_ARGS(skb, location));

#endif
