/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
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
#ifndef _REGRESS_H_
#define _REGRESS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "tinytest.h"
#include "tinytest_macros.h"

extern struct testcase_t legacy_testcases[];
extern struct testcase_t util_testcases[];
extern struct testcase_t signal_testcases[];
extern struct testcase_t http_testcases[];
extern struct testcase_t dns_testcases[];

int legacy_main(void);

void rpc_suite(void);

void regress_pthread(void);
void regress_zlib(void);

void test_edgetriggered(void);

/* Helpers to wrap old testcases */
extern int pair[2];
extern int test_ok;
extern int called;
extern struct event_base *global_base;
extern int in_legacy_test_wrapper;

extern const struct testcase_setup_t legacy_setup;
void run_legacy_test_fn(void *ptr);

/* A couple of flags that legacy_setup can support. */
#define TT_NEED_SOCKETPAIR   TT_FIRST_USER_FLAG
#define TT_NEED_BASE         (TT_FIRST_USER_FLAG<<1)
#define TT_NEED_DNS          (TT_FIRST_USER_FLAG<<2)

/* All the flags that a legacy test needs. */
#define TT_ISOLATED TT_FORK|TT_NEED_SOCKETPAIR|TT_NEED_BASE

#define LEGACY(name,flags)						\
	{ #name, run_legacy_test_fn, flags, &legacy_setup,		\
	  test_## name }


#ifdef __cplusplus
}
#endif

#endif /* _REGRESS_H_ */
