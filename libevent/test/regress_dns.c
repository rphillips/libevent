/*
 * Copyright (c) 2003-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2009 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#endif

#include "event-config.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef _EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#ifndef WIN32
#include <sys/socket.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#ifdef _EVENT_HAVE_NETINET_IN6_H
#include <netinet/in6.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "event2/event.h"
#include "event2/event_compat.h"
#include <event2/util.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include "evdns.h"
#include "log-internal.h"
#include "regress.h"

static int dns_ok = 0;
static int dns_got_cancel = 0;
static int dns_err = 0;

static void
dns_gethostbyname_cb(int result, char type, int count, int ttl,
    void *addresses, void *arg)
{
	dns_ok = dns_err = 0;

	if (result == DNS_ERR_TIMEOUT) {
		printf("[Timed out] ");
		dns_err = result;
		goto out;
	}

	if (result != DNS_ERR_NONE) {
		printf("[Error code %d] ", result);
		goto out;
	}

        TT_BLATHER(("type: %d, count: %d, ttl: %d: ", type, count, ttl));

	switch (type) {
	case DNS_IPv6_AAAA: {
#if defined(_EVENT_HAVE_STRUCT_IN6_ADDR) && defined(_EVENT_HAVE_INET_NTOP) && defined(INET6_ADDRSTRLEN)
		struct in6_addr *in6_addrs = addresses;
		char buf[INET6_ADDRSTRLEN+1];
		int i;
		/* a resolution that's not valid does not help */
		if (ttl < 0)
			goto out;
		for (i = 0; i < count; ++i) {
			const char *b = inet_ntop(AF_INET6, &in6_addrs[i], buf,sizeof(buf));
			if (b)
				TT_BLATHER(("%s ", b));
			else
				TT_BLATHER(("%s ", strerror(errno)));
		}
#endif
		break;
	}
	case DNS_IPv4_A: {
		struct in_addr *in_addrs = addresses;
		int i;
		/* a resolution that's not valid does not help */
		if (ttl < 0)
			goto out;
		for (i = 0; i < count; ++i)
                        TT_BLATHER(("%s ", inet_ntoa(in_addrs[i])));
		break;
	}
	case DNS_PTR:
		/* may get at most one PTR */
		if (count != 1)
			goto out;

		TT_BLATHER(("%s ", *(char **)addresses));
		break;
	default:
		goto out;
	}

	dns_ok = type;

out:
	if (arg == NULL)
		event_loopexit(NULL);
	else
		event_base_loopexit((struct event_base *)arg, NULL);
}

static void
dns_gethostbyname(void)
{
	dns_ok = 0;
	evdns_resolve_ipv4("www.monkey.org", 0, dns_gethostbyname_cb, NULL);
	event_dispatch();

        tt_int_op(dns_ok, ==, DNS_IPv4_A);
        test_ok = dns_ok;
end:
        ;
}

static void
dns_gethostbyname6(void)
{
	dns_ok = 0;
	evdns_resolve_ipv6("www.ietf.org", 0, dns_gethostbyname_cb, NULL);
	event_dispatch();

        if (!dns_ok && dns_err == DNS_ERR_TIMEOUT) {
                tt_skip();
        }

        tt_int_op(dns_ok, ==, DNS_IPv6_AAAA);
        test_ok = 1;
end:
        ;
}

static void
dns_gethostbyaddr(void)
{
	struct in_addr in;
	in.s_addr = htonl(0x7f000001ul); /* 127.0.0.1 */
	dns_ok = 0;
	evdns_resolve_reverse(&in, 0, dns_gethostbyname_cb, NULL);
	event_dispatch();

        tt_int_op(dns_ok, ==, DNS_PTR);
        test_ok = dns_ok;
end:
        ;
}

static void
dns_resolve_reverse(void *ptr)
{
	struct in_addr in;
	struct event_base *base = event_base_new();
	struct evdns_base *dns = evdns_base_new(base, 1/* init name servers */);
	struct evdns_request *req = NULL;

        tt_assert(base);
        tt_assert(dns);
	in.s_addr = htonl(0x7f000001ul); /* 127.0.0.1 */
	dns_ok = 0;

	req = evdns_base_resolve_reverse(
		dns, &in, 0, dns_gethostbyname_cb, base);
        tt_assert(req);

	event_base_dispatch(base);

        tt_int_op(dns_ok, ==, DNS_PTR);

end:
        if (dns)
                evdns_base_free(dns, 0);
        if (base)
                event_base_free(base);
}

static int n_server_responses = 0;

static void
dns_server_request_cb(struct evdns_server_request *req, void *data)
{
	int i, r;
	const char TEST_ARPA[] = "11.11.168.192.in-addr.arpa";
	const char TEST_IN6[] =
	    "f.e.f.e." "0.0.0.0." "0.0.0.0." "1.1.1.1."
	    "a.a.a.a." "0.0.0.0." "0.0.0.0." "0.f.f.f.ip6.arpa";

	for (i = 0; i < req->nquestions; ++i) {
		const int qtype = req->questions[i]->type;
		const int qclass = req->questions[i]->dns_question_class;
		const char *qname = req->questions[i]->name;

		struct in_addr ans;
		ans.s_addr = htonl(0xc0a80b0bUL); /* 192.168.11.11 */
		if (qtype == EVDNS_TYPE_A &&
		    qclass == EVDNS_CLASS_INET &&
		    !evutil_ascii_strcasecmp(qname, "zz.example.com")) {
			r = evdns_server_request_add_a_reply(req, qname,
			    1, &ans.s_addr, 12345);
			if (r<0)
				dns_ok = 0;
		} else if (qtype == EVDNS_TYPE_AAAA &&
		    qclass == EVDNS_CLASS_INET &&
		    !evutil_ascii_strcasecmp(qname, "zz.example.com")) {
			char addr6[17] = "abcdefghijklmnop";
			r = evdns_server_request_add_aaaa_reply(req,
			    qname, 1, addr6, 123);
			if (r<0)
				dns_ok = 0;
		} else if (qtype == EVDNS_TYPE_PTR &&
		    qclass == EVDNS_CLASS_INET &&
		    !evutil_ascii_strcasecmp(qname, TEST_ARPA)) {
			r = evdns_server_request_add_ptr_reply(req, NULL,
			    qname, "ZZ.EXAMPLE.COM", 54321);
			if (r<0)
				dns_ok = 0;
		} else if (qtype == EVDNS_TYPE_PTR &&
		    qclass == EVDNS_CLASS_INET &&
		    !evutil_ascii_strcasecmp(qname, TEST_IN6)){
			r = evdns_server_request_add_ptr_reply(req, NULL,
			    qname,
			    "ZZ-INET6.EXAMPLE.COM", 54322);
			if (r<0)
				dns_ok = 0;
                } else if (qtype == EVDNS_TYPE_A &&
		    qclass == EVDNS_CLASS_INET &&
		    !evutil_ascii_strcasecmp(qname, "drop.example.com")) {
			if (evdns_server_request_drop(req)<0)
				dns_ok = 0;
			return;
		} else {
			printf("Unexpected question %d %d \"%s\" ",
			    qtype, qclass, qname);
			dns_ok = 0;
		}
	}
	r = evdns_server_request_respond(req, 0);
	if (r<0) {
		printf("Couldn't send reply. ");
		dns_ok = 0;
	}
}

static void
dns_server_gethostbyname_cb(int result, char type, int count, int ttl,
    void *addresses, void *arg)
{
	if (result == DNS_ERR_CANCEL) {
		if (arg != (void*)(char*)90909) {
			printf("Unexpected cancelation");
			dns_ok = 0;
		}
		dns_got_cancel = 1;
		goto out;
	}
	if (result != DNS_ERR_NONE) {
		printf("Unexpected result %d. ", result);
		dns_ok = 0;
		goto out;
	}
	if (count != 1) {
		printf("Unexpected answer count %d. ", count);
		dns_ok = 0;
		goto out;
	}
	switch (type) {
	case DNS_IPv4_A: {
		struct in_addr *in_addrs = addresses;
		if (in_addrs[0].s_addr != htonl(0xc0a80b0bUL) || ttl != 12345) {
			printf("Bad IPv4 response \"%s\" %d. ",
					inet_ntoa(in_addrs[0]), ttl);
			dns_ok = 0;
			goto out;
		}
		break;
	}
	case DNS_IPv6_AAAA: {
#if defined (_EVENT_HAVE_STRUCT_IN6_ADDR) && defined(_EVENT_HAVE_INET_NTOP) && defined(INET6_ADDRSTRLEN)
		struct in6_addr *in6_addrs = addresses;
		char buf[INET6_ADDRSTRLEN+1];
		if (memcmp(&in6_addrs[0].s6_addr, "abcdefghijklmnop", 16)
		    || ttl != 123) {
			const char *b = inet_ntop(AF_INET6, &in6_addrs[0],buf,sizeof(buf));
			printf("Bad IPv6 response \"%s\" %d. ", b, ttl);
			dns_ok = 0;
			goto out;
		}
#endif
		break;
	}
	case DNS_PTR: {
		char **addrs = addresses;
		if (arg != (void*)6) {
			if (strcmp(addrs[0], "ZZ.EXAMPLE.COM") ||
			    ttl != 54321) {
				printf("Bad PTR response \"%s\" %d. ",
				    addrs[0], ttl);
				dns_ok = 0;
				goto out;
			}
		} else {
			if (strcmp(addrs[0], "ZZ-INET6.EXAMPLE.COM") ||
			    ttl != 54322) {
				printf("Bad ipv6 PTR response \"%s\" %d. ",
				    addrs[0], ttl);
				dns_ok = 0;
				goto out;
			}
		}
		break;
	}
	default:
		printf("Bad response type %d. ", type);
		dns_ok = 0;
	}
 out:
	if (++n_server_responses == 3) {
		event_loopexit(NULL);
	}
}

static void
dns_server(void)
{
        evutil_socket_t sock=-1;
	struct sockaddr_in my_addr;
	struct evdns_server_port *port=NULL;
	struct in_addr resolve_addr;
	struct in6_addr resolve_addr6;
	struct evdns_base *base=NULL;
	struct evdns_request *req=NULL;

	dns_ok = 1;

	base = evdns_base_new(NULL, 0);

	/* Add ourself as the only nameserver, and make sure we really are
	 * the only nameserver. */
	evdns_base_nameserver_ip_add(base, "127.0.0.1:35353");

	tt_int_op(evdns_base_count_nameservers(base), ==, 1);
	/* Now configure a nameserver port. */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock<0) {
                tt_abort_perror("socket");
        }

        evutil_make_socket_nonblocking(sock);

	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(35353);
	my_addr.sin_addr.s_addr = htonl(0x7f000001UL);
	if (bind(sock, (struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
		tt_abort_perror("bind");
	}
	port = evdns_add_server_port(sock, 0, dns_server_request_cb, NULL);

	/* Send some queries. */
	evdns_base_resolve_ipv4(base, "zz.example.com", DNS_QUERY_NO_SEARCH,
					   dns_server_gethostbyname_cb, NULL);
	evdns_base_resolve_ipv6(base, "zz.example.com", DNS_QUERY_NO_SEARCH,
					   dns_server_gethostbyname_cb, NULL);
	resolve_addr.s_addr = htonl(0xc0a80b0bUL); /* 192.168.11.11 */
	evdns_base_resolve_reverse(base, &resolve_addr, 0,
            dns_server_gethostbyname_cb, NULL);
	memcpy(resolve_addr6.s6_addr,
	    "\xff\xf0\x00\x00\x00\x00\xaa\xaa"
	    "\x11\x11\x00\x00\x00\x00\xef\xef", 16);
	evdns_base_resolve_reverse_ipv6(base, &resolve_addr6, 0,
            dns_server_gethostbyname_cb, (void*)6);

	req = evdns_base_resolve_ipv4(base,
	    "drop.example.com", DNS_QUERY_NO_SEARCH,
	    dns_server_gethostbyname_cb, (void*)(char*)90909);

	evdns_cancel_request(base, req);

	event_dispatch();

	tt_assert(dns_got_cancel);
        test_ok = dns_ok;

end:
        if (port)
                evdns_close_server_port(port);
        if (sock >= 0)
                EVUTIL_CLOSESOCKET(sock);
	if (base)
		evdns_base_free(base, 0);
}

struct generic_dns_server_table {
	const char *q;
	const char *anstype;
	const char *ans;
	int seen;
};

static void
generic_dns_server_cb(struct evdns_server_request *req, void *data)
{
	struct generic_dns_server_table *tab = data;
	const char *question;

	if (req->nquestions != 1)
		TT_DIE(("Only handling one question at a time; got %d",
			req->nquestions));

	question = req->questions[0]->name;

	while (tab->q && evutil_ascii_strcasecmp(question, tab->q) &&
	    strcmp("*", tab->q))
		++tab;
	if (tab->q == NULL)
		TT_DIE(("Unexpected question: '%s'", question));

	++tab->seen;

	if (!strcmp(tab->anstype, "err")) {
		int err = atoi(tab->ans);
		tt_assert(! evdns_server_request_respond(req, err));
		return;
	} else if (!strcmp(tab->anstype, "A")) {
		struct in_addr in;
		evutil_inet_pton(AF_INET, tab->ans, &in);
		evdns_server_request_add_a_reply(req, question, 1, &in.s_addr,
		    100);
	} else if (!strcmp(tab->anstype, "AAAA")) {
		struct in6_addr in6;
		evutil_inet_pton(AF_INET6, tab->ans, &in6);
		evdns_server_request_add_aaaa_reply(req,
		    question, 1, &in6.s6_addr, 100);
	} else {
		TT_DIE(("Weird table entry with type '%s'", tab->anstype));
	}
	tt_assert(! evdns_server_request_respond(req, 0))
	return;
end:
	tt_want(! evdns_server_request_drop(req));
}

static struct evdns_server_port *
get_generic_server(struct event_base *base,
    ev_uint16_t portnum,
    evdns_request_callback_fn_type cb,
    void *arg)
{
	struct evdns_server_port *port = NULL;
	evutil_socket_t sock;
	struct sockaddr_in my_addr;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock<=0) {
                tt_abort_perror("socket");
        }

        evutil_make_socket_nonblocking(sock);

	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(portnum);
	my_addr.sin_addr.s_addr = htonl(0x7f000001UL);
	if (bind(sock, (struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
		tt_abort_perror("bind");
	}
	port = evdns_add_server_port_with_base(base, sock, 0, cb, arg);

	return port;
end:
	return NULL;
}

static int n_replies_left;
static struct event_base *exit_base;

struct generic_dns_callback_result {
	int result;
	char type;
	int count;
	int ttl;
	void *addrs;
};

static void
generic_dns_callback(int result, char type, int count, int ttl, void *addresses,
    void *arg)
{
	size_t len;
	struct generic_dns_callback_result *res = arg;
	res->result = result;
	res->type = type;
	res->count = count;
	res->ttl = ttl;

	if (type == DNS_IPv4_A)
		len = count * 4;
	else if (type == DNS_IPv6_AAAA)
		len = count * 16;
	else if (type == DNS_PTR)
		len = strlen(addresses)+1;
	else {
		len = 0;
		res->addrs = NULL;
	}
	if (len) {
		res->addrs = malloc(len);
		memcpy(res->addrs, addresses, len);
	}

	if (--n_replies_left == 0)
		event_base_loopexit(exit_base, NULL);
}

static struct generic_dns_server_table search_table[] = {
	{ "host.a.example.com", "err", "3", 0 },
	{ "host.b.example.com", "err", "3", 0 },
	{ "host.c.example.com", "A", "11.22.33.44", 0 },
	{ "host2.a.example.com", "err", "3", 0 },
	{ "host2.b.example.com", "A", "200.100.0.100", 0 },
	{ "host2.c.example.com", "err", "3", 0 },

	{ "host", "err", "3", 0 },
	{ "host2", "err", "3", 0 },
	{ "*", "err", "3", 0 },
	{ NULL, NULL, NULL, 0 }
};

static void
dns_search_test(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;
	struct evdns_server_port *port = NULL;
	struct evdns_base *dns = NULL;

	struct generic_dns_callback_result r1, r2, r3, r4, r5;

	port = get_generic_server(base, 53900, generic_dns_server_cb,
	    search_table);
	tt_assert(port);

	dns = evdns_base_new(base, 0);
	tt_assert(!evdns_base_nameserver_ip_add(dns, "127.0.0.1:53900"));

	evdns_base_search_add(dns, "a.example.com");
	evdns_base_search_add(dns, "b.example.com");
	evdns_base_search_add(dns, "c.example.com");

	n_replies_left = 5;
	exit_base = base;

	evdns_base_resolve_ipv4(dns, "host", 0, generic_dns_callback, &r1);
	evdns_base_resolve_ipv4(dns, "host2", 0, generic_dns_callback, &r2);
	evdns_base_resolve_ipv4(dns, "host", DNS_NO_SEARCH, generic_dns_callback, &r3);
	evdns_base_resolve_ipv4(dns, "host2", DNS_NO_SEARCH, generic_dns_callback, &r4);
	evdns_base_resolve_ipv4(dns, "host3", 0, generic_dns_callback, &r5);

	event_base_dispatch(base);

	tt_int_op(r1.type, ==, DNS_IPv4_A);
	tt_int_op(r1.count, ==, 1);
	tt_int_op(((ev_uint32_t*)r1.addrs)[0], ==, htonl(0x0b16212c));
	tt_int_op(r2.type, ==, DNS_IPv4_A);
	tt_int_op(r2.count, ==, 1);
	tt_int_op(((ev_uint32_t*)r2.addrs)[0], ==, htonl(0xc8640064));
	tt_int_op(r3.result, ==, DNS_ERR_NOTEXIST);
	tt_int_op(r4.result, ==, DNS_ERR_NOTEXIST);
	tt_int_op(r5.result, ==, DNS_ERR_NOTEXIST);

end:
	if (dns)
		evdns_base_free(dns, 0);
	if (port)
		evdns_close_server_port(port);
}

static void
fail_server_cb(struct evdns_server_request *req, void *data)
{
	const char *question;
	int *count = data;
	struct in_addr in;

	/* Drop the first N requests that we get. */
	if (*count > 0) {
		--*count;
		tt_want(! evdns_server_request_drop(req));
		return;
	}

	if (req->nquestions != 1)
		TT_DIE(("Only handling one question at a time; got %d",
			req->nquestions));

	question = req->questions[0]->name;

	if (!evutil_ascii_strcasecmp(question, "google.com")) {
		/* Detect a probe, and get out of the loop. */
		event_base_loopexit(exit_base, NULL);
	}

	evutil_inet_pton(AF_INET, "16.32.64.128", &in);
	evdns_server_request_add_a_reply(req, question, 1, &in.s_addr,
	    100);
	tt_assert(! evdns_server_request_respond(req, 0))
	return;
end:
	tt_want(! evdns_server_request_drop(req));
}

static void
dns_retry_test(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;
	struct evdns_server_port *port = NULL;
	struct evdns_base *dns = NULL;
	int drop_count = 2;

	struct generic_dns_callback_result r1;

	port = get_generic_server(base, 53900, fail_server_cb,
	    &drop_count);
	tt_assert(port);

	dns = evdns_base_new(base, 0);
	tt_assert(!evdns_base_nameserver_ip_add(dns, "127.0.0.1:53900"));
	tt_assert(! evdns_base_set_option(dns, "timeout:", "0.3", DNS_OPTIONS_ALL));
	tt_assert(! evdns_base_set_option(dns, "max-timeouts:", "10", DNS_OPTIONS_ALL));

	evdns_base_resolve_ipv4(dns, "host.example.com", 0,
	    generic_dns_callback, &r1);

	n_replies_left = 1;
	exit_base = base;

	event_base_dispatch(base);

	tt_int_op(drop_count, ==, 0);

	tt_int_op(r1.type, ==, DNS_IPv4_A);
	tt_int_op(r1.count, ==, 1);
	tt_int_op(((ev_uint32_t*)r1.addrs)[0], ==, htonl(0x10204080));


	/* Now try again, but this time have the server get treated as
	 * failed, so we can send it a test probe. */
	drop_count = 4;
	tt_assert(! evdns_base_set_option(dns, "max-timeouts:", "3", DNS_OPTIONS_ALL));
	tt_assert(! evdns_base_set_option(dns, "attempts:", "4", DNS_OPTIONS_ALL));
	memset(&r1, 0, sizeof(r1));

	evdns_base_resolve_ipv4(dns, "host.example.com", 0,
	    generic_dns_callback, &r1);

	n_replies_left = 2;

	/* This will run until it answers the "google.com" probe request. */
	/* XXXX It takes 10 seconds to retry the probe, which makes the test
	 * slow. */
	event_base_dispatch(base);

	/* We'll treat the server as failed here. */
	tt_int_op(r1.result, ==, DNS_ERR_TIMEOUT);

	/* It should work this time. */
	tt_int_op(drop_count, ==, 0);
	evdns_base_resolve_ipv4(dns, "host.example.com", 0,
	    generic_dns_callback, &r1);

	event_base_dispatch(base);
	tt_int_op(r1.result, ==, DNS_ERR_NONE);
	tt_int_op(r1.type, ==, DNS_IPv4_A);
	tt_int_op(r1.count, ==, 1);
	tt_int_op(((ev_uint32_t*)r1.addrs)[0], ==, htonl(0x10204080));

end:
	if (dns)
		evdns_base_free(dns, 0);
	if (port)
		evdns_close_server_port(port);
}

static struct generic_dns_server_table internal_error_table[] = {
	/* Error 4 (NOTIMPL) makes us reissue the request to another server
	   if we can.

	   XXXX we should reissue under a much wider set of circumstances!
	 */
	{ "foof.example.com", "err", "4", 0 },
	{ NULL, NULL, NULL, 0 }
};

static struct generic_dns_server_table reissue_table[] = {
	{ "foof.example.com", "A", "240.15.240.15", 0 },
	{ NULL, NULL, NULL, 0 }
};

static void
dns_reissue_test(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;
	struct evdns_server_port *port1 = NULL, *port2 = NULL;
	struct evdns_base *dns = NULL;
	struct generic_dns_callback_result r1;

	port1 = get_generic_server(base, 53900, generic_dns_server_cb,
	    internal_error_table);
	tt_assert(port1);
	port2 = get_generic_server(base, 53901, generic_dns_server_cb,
	    reissue_table);
	tt_assert(port2);

	dns = evdns_base_new(base, 0);
	tt_assert(!evdns_base_nameserver_ip_add(dns, "127.0.0.1:53900"));
	tt_assert(! evdns_base_set_option(dns, "timeout:", "0.3", DNS_OPTIONS_ALL));
	tt_assert(! evdns_base_set_option(dns, "max-timeouts:", "2", DNS_OPTIONS_ALL));
	tt_assert(! evdns_base_set_option(dns, "attempts:", "5", DNS_OPTIONS_ALL));

	memset(&r1, 0, sizeof(r1));
	evdns_base_resolve_ipv4(dns, "foof.example.com", 0,
	    generic_dns_callback, &r1);

	/* Add this after, so that we are sure to get a reissue. */
	tt_assert(!evdns_base_nameserver_ip_add(dns, "127.0.0.1:53901"));

	n_replies_left = 1;
	exit_base = base;

	event_base_dispatch(base);
	tt_int_op(r1.result, ==, DNS_ERR_NONE);
	tt_int_op(r1.type, ==, DNS_IPv4_A);
	tt_int_op(r1.count, ==, 1);
	tt_int_op(((ev_uint32_t*)r1.addrs)[0], ==, htonl(0xf00ff00f));

	/* Make sure we dropped at least once. */
	tt_int_op(internal_error_table[0].seen, >, 0);

end:
	if (dns)
		evdns_base_free(dns, 0);
	if (port1)
		evdns_close_server_port(port1);
	if (port2)
		evdns_close_server_port(port2);
}

static void
dumb_bytes_fn(char *p, size_t n)
{
	unsigned i;
	/* This gets us 6 bits of entropy per transaction ID, which means we
	 * will have probably have collisions and need to pick again. */
	for(i=0;i<n;++i)
		p[i] = (char)(rand() & 7);
}

static void
dns_inflight_test(void *arg)
{
	struct basic_test_data *data = arg;
	struct event_base *base = data->base;
	struct evdns_server_port *port = NULL;
	struct evdns_base *dns = NULL;

	struct generic_dns_callback_result r[20];
	int i;

	port = get_generic_server(base, 53900, generic_dns_server_cb,
	    reissue_table);
	tt_assert(port);

	/* Make sure that having another (very bad!) RNG doesn't mess us
	 * up. */
	evdns_set_random_bytes_fn(dumb_bytes_fn);

	dns = evdns_base_new(base, 0);
	tt_assert(!evdns_base_nameserver_ip_add(dns, "127.0.0.1:53900"));
	tt_assert(! evdns_base_set_option(dns, "max-inflight:", "3", DNS_OPTIONS_ALL));
	tt_assert(! evdns_base_set_option(dns, "randomize-case:", "0", DNS_OPTIONS_ALL));

	for(i=0;i<20;++i)
		evdns_base_resolve_ipv4(dns, "foof.example.com", 0, generic_dns_callback, &r[i]);

	n_replies_left = 20;
	exit_base = base;

	event_base_dispatch(base);

	for (i=0;i<20;++i) {
		tt_int_op(r[i].type, ==, DNS_IPv4_A);
		tt_int_op(r[i].count, ==, 1);
		tt_int_op(((ev_uint32_t*)r[i].addrs)[0], ==, htonl(0xf00ff00f));
	}

end:
	if (dns)
		evdns_base_free(dns, 0);
	if (port)
		evdns_close_server_port(port);
}

/* === Test for bufferevent_socket_connect_hostname */

static int total_connected_or_failed = 0;
static struct event_base *be_connect_hostname_base = NULL;

/* Implements a DNS server for the connect_hostname test. */
static void
be_connect_hostname_server_cb(struct evdns_server_request *req, void *data)
{
	int i;
	int *n_got_p=data;
	int added_any=0;
	++*n_got_p;

	for (i=0;i<req->nquestions;++i) {
		const int qtype = req->questions[i]->type;
		const int qclass = req->questions[i]->dns_question_class;
		const char *qname = req->questions[i]->name;
		struct in_addr ans;

		if (qtype == EVDNS_TYPE_A &&
		    qclass == EVDNS_CLASS_INET &&
		    !evutil_ascii_strcasecmp(qname, "nobodaddy.example.com")) {
			ans.s_addr = htonl(0x7f000001);
			evdns_server_request_add_a_reply(req, qname,
			    1, &ans.s_addr, 2000);
			added_any = 1;
		} else if (!evutil_ascii_strcasecmp(qname,
			"nosuchplace.example.com")) {
			/* ok, just say notfound. */
		} else {
			TT_GRIPE(("Got weird request for %s",qname));
		}
	}
	if (added_any)
		evdns_server_request_respond(req, 0);
	else
		evdns_server_request_respond(req, 3);
}

/* Implements a listener for connect_hostname test. */
static void
nil_accept_cb(struct evconnlistener *l, evutil_socket_t fd, struct sockaddr *s,
    int socklen, void *arg)
{
	int *p = arg;
	(*p)++;
	/* don't do anything with the socket; let it close when we exit() */
}

/* Helper: return the port that a socket is bound on, in host order. */
static int
get_socket_port(evutil_socket_t fd)
{
	struct sockaddr_storage ss;
	ev_socklen_t socklen = sizeof(ss);
	if (getsockname(fd, (struct sockaddr*)&ss, &socklen) != 0)
		return -1;
	if (ss.ss_family == AF_INET)
		return ntohs( ((struct sockaddr_in*)&ss)->sin_port);
	else if (ss.ss_family == AF_INET6)
		return ntohs( ((struct sockaddr_in6*)&ss)->sin6_port);
	else
		return -1;
}

/* Bufferevent event callback for the connect_hostname test: remembers what
 * event we got. */
static void
be_connect_hostname_event_cb(struct bufferevent *bev, short what, void *ctx)
{
	int *got = ctx;
	if (!*got) {
		TT_BLATHER(("Got a bufferevent event %d", what));
		*got = what;

		if ((what & BEV_EVENT_CONNECTED) || (what & BEV_EVENT_ERROR)) {
			++total_connected_or_failed;
			if (total_connected_or_failed >= 5)
				event_base_loopexit(be_connect_hostname_base,
				    NULL);
		}
	} else {
		TT_FAIL(("Two events on one bufferevent. %d,%d",
			(int)*got, (int)what));
	}
}

static void
test_bufferevent_connect_hostname(void *arg)
{
	struct basic_test_data *data = arg;
	struct evconnlistener *listener = NULL;
	struct bufferevent *be1=NULL, *be2=NULL, *be3=NULL, *be4=NULL, *be5=NULL;
	int be1_outcome=0, be2_outcome=0, be3_outcome=0, be4_outcome=0,
	    be5_outcome=0;
	struct evdns_base *dns=NULL;
	struct evdns_server_port *port=NULL;
	evutil_socket_t server_fd=-1;
	struct sockaddr_in sin;
	int listener_port=-1, dns_port=-1;
	int n_accept=0, n_dns=0;
	char buf[128];

	be_connect_hostname_base = data->base;

	/* Bind an address and figure out what port it's on. */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0x7f000001); /* 127.0.0.1 */
	sin.sin_port = 0;
	listener = evconnlistener_new_bind(data->base, nil_accept_cb,
	    &n_accept,
	    LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_EXEC,
	    -1, (struct sockaddr *)&sin, sizeof(sin));
	listener_port = get_socket_port(evconnlistener_get_fd(listener));

	/* Start an evdns server that resolves nobodaddy.example.com to
	 * 127.0.0.1 */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0x7f000001); /* 127.0.0.1 */
	sin.sin_port = 0;
	server_fd = socket(AF_INET, SOCK_DGRAM, 0);
	tt_int_op(server_fd, >=, 0);
	if (bind(server_fd, (struct sockaddr*)&sin, sizeof(sin))<0) {
		tt_abort_perror("bind");
	}
        evutil_make_socket_nonblocking(server_fd);
	dns_port = get_socket_port(server_fd);
	port = evdns_add_server_port_with_base(data->base, server_fd, 0,
	    be_connect_hostname_server_cb, &n_dns);

	/* Start an evdns_base that uses the server as its resolver. */
	dns = evdns_base_new(data->base, 0);
	evutil_snprintf(buf, sizeof(buf), "127.0.0.1:%d", dns_port);
	evdns_base_nameserver_ip_add(dns, buf);

	/* Now, finally, at long last, launch the bufferevents.  One should do
	 * a failing lookup IP, one should do a successful lookup by IP,
	 * and one should do a successful lookup by hostname. */
	be1 = bufferevent_socket_new(data->base, -1, BEV_OPT_CLOSE_ON_FREE);
	be2 = bufferevent_socket_new(data->base, -1, BEV_OPT_CLOSE_ON_FREE);
	be3 = bufferevent_socket_new(data->base, -1, BEV_OPT_CLOSE_ON_FREE);
	be4 = bufferevent_socket_new(data->base, -1, BEV_OPT_CLOSE_ON_FREE);
	be5 = bufferevent_socket_new(data->base, -1, BEV_OPT_CLOSE_ON_FREE);

	bufferevent_setcb(be1, NULL, NULL, be_connect_hostname_event_cb,
	    &be1_outcome);
	bufferevent_setcb(be2, NULL, NULL, be_connect_hostname_event_cb,
	    &be2_outcome);
	bufferevent_setcb(be3, NULL, NULL, be_connect_hostname_event_cb,
	    &be3_outcome);
	bufferevent_setcb(be4, NULL, NULL, be_connect_hostname_event_cb,
	    &be4_outcome);
	bufferevent_setcb(be5, NULL, NULL, be_connect_hostname_event_cb,
	    &be5_outcome);

	/* Launch an async resolve that will fail. */
	tt_assert(!bufferevent_socket_connect_hostname(be1, dns, AF_INET,
		"nosuchplace.example.com", listener_port));
	/* Connect to the IP without resolving. */
	tt_assert(!bufferevent_socket_connect_hostname(be2, dns, AF_INET,
		"127.0.0.1", listener_port));
	/* Launch an async resolve that will succeed. */
	tt_assert(!bufferevent_socket_connect_hostname(be3, dns, AF_INET,
		"nobodaddy.example.com", listener_port));
	/* Use the blocking resolver.  This one will fail if your resolver
	 * can't resolve localhost to 127.0.0.1 */
	tt_assert(!bufferevent_socket_connect_hostname(be4, NULL, AF_INET,
		"localhost", listener_port));
	/* Use the blocking resolver with a nonexistent hostname. */
	tt_assert(bufferevent_socket_connect_hostname(be5, NULL, AF_INET,
		"nonesuch.nowhere.example.com", 80) < 0);

	event_base_dispatch(data->base);

	tt_int_op(be1_outcome, ==, BEV_EVENT_ERROR);
	tt_int_op(be2_outcome, ==, BEV_EVENT_CONNECTED);
	tt_int_op(be3_outcome, ==, BEV_EVENT_CONNECTED);
	tt_int_op(be4_outcome, ==, BEV_EVENT_CONNECTED);
	tt_int_op(be5_outcome, ==, BEV_EVENT_ERROR);

	tt_int_op(n_accept, ==, 3);
	tt_int_op(n_dns, ==, 2);

end:
	if (listener)
		evconnlistener_free(listener);
	if (server_fd>=0)
		EVUTIL_CLOSESOCKET(server_fd);
	if (port)
                evdns_close_server_port(port);
	if (dns)
		evdns_base_free(dns, 0);
	if (be1)
		bufferevent_free(be1);
	if (be2)
		bufferevent_free(be2);
	if (be3)
		bufferevent_free(be3);
	if (be4)
		bufferevent_free(be4);
	if (be5)
		bufferevent_free(be5);
}

#define DNS_LEGACY(name, flags)                                        \
	{ #name, run_legacy_test_fn, flags|TT_LEGACY, &legacy_setup,   \
                    dns_##name }

struct testcase_t dns_testcases[] = {
        DNS_LEGACY(server, TT_FORK|TT_NEED_BASE),
        DNS_LEGACY(gethostbyname, TT_FORK|TT_NEED_BASE|TT_NEED_DNS),
        DNS_LEGACY(gethostbyname6, TT_FORK|TT_NEED_BASE|TT_NEED_DNS),
        DNS_LEGACY(gethostbyaddr, TT_FORK|TT_NEED_BASE|TT_NEED_DNS),
        { "resolve_reverse", dns_resolve_reverse, TT_FORK, NULL, NULL },
	{ "search", dns_search_test, TT_FORK|TT_NEED_BASE, &basic_setup, NULL },
	{ "retry", dns_retry_test, TT_FORK|TT_NEED_BASE, &basic_setup, NULL },
	{ "reissue", dns_reissue_test, TT_FORK|TT_NEED_BASE, &basic_setup, NULL },
	{ "inflight", dns_inflight_test, TT_FORK|TT_NEED_BASE, &basic_setup, NULL },
	{ "bufferevent_connnect_hostname", test_bufferevent_connect_hostname,
	  TT_FORK|TT_NEED_BASE, &basic_setup, NULL },

        END_OF_TESTCASES
};

