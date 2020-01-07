#include "url_parser.h"
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "zarray.h"

struct url_parser {
    // consider the URL:
    // http://www.google.com:80/search?q=a
    //
    char *protocol; // e.g. http://
    char *host;     // e.g. www.google.com
    int  port;
    char *path;     // e.g. /search

    zarray_t *keys;
    zarray_t *vals;
};

static int
strposat (const char *haystack, const char *needle, int haystackpos)
{
    int idx = haystackpos;
    int needlelen = strlen(needle);

    while (haystack[idx] != 0) {
        if (!strncmp(&haystack[idx], needle, needlelen))
            return idx;

        idx++;
    }

    return -1; // not found.
}

static int
strpos (const char *haystack, const char *needle)
{
    return strposat(haystack, needle, 0);
}

url_parser_t *
url_parser_create (const char *s)
{
    url_parser_t *urlp = calloc(1, sizeof(*urlp));

    int slen = strlen(s);

    urlp->keys = zarray_create(sizeof(char*));
    urlp->vals = zarray_create(sizeof(char*));

    int hostpos = strpos(s, "://") + 3;
    urlp->protocol = strndup(s, hostpos);

    // extract the hostport. it's between the slashpos and terminated by
    // either the third slash or a question mark or end of string.
    int hostportend = strposat(s, "/", hostpos);
    if (hostportend < 0)
        hostportend = strposat(s, "?", hostpos);
    if (hostportend < 0)
        hostportend = slen;

    char *hostport = strndup(&s[hostpos], hostportend-hostpos);

    // extract the path: it's between the 3rd slash and terminated by
    // either a ? or the end.
    int thirdslashpos = strposat(s, "/", hostpos);
    if (thirdslashpos >= 0) {
        int endpathpos = strposat(s, "?", thirdslashpos);
        if (endpathpos < 0)
            endpathpos = slen;

        urlp->path = strndup(&s[thirdslashpos], endpathpos - thirdslashpos );
    } else {
        urlp->path = strdup("/");
    }

    // e.g. "/search?q=a"
    int parampos = strpos(s, "?");

    while (parampos >= 0) {
        int nextparampos = strposat(s, "&", parampos+1);

        int eqpos = strposat(s, "=", parampos+1);
        char *key = strndup(&s[parampos+1], eqpos - parampos-1);
        char *val = strndup(&s[eqpos+1], nextparampos < 0 ? 9999 : nextparampos - eqpos - 1);

        zarray_add(urlp->keys, &key);
        zarray_add(urlp->vals, &val);

        parampos = nextparampos;
    }

    // chop up hostport into host and port.
    int colonpos = strpos(hostport, ":");
    if (colonpos >= 0) {
        urlp->host = strndup(hostport, colonpos);
        urlp->port = atoi(&hostport[colonpos+1]);
    } else {
        urlp->host = strdup(hostport);
        urlp->port = -1;
    }

    free(hostport);

    return urlp;
}

void
url_parser_destroy (url_parser_t *urlp)
{
    free(urlp->protocol);
    free(urlp->host);
    free(urlp->path);
    zarray_vmap(urlp->keys, free);
    zarray_vmap(urlp->vals, free);
    zarray_destroy(urlp->keys);
    zarray_destroy(urlp->vals);
    free(urlp);
}

// e.g., "http://"
const char *
url_parser_get_protocol (url_parser_t *urlp)
{
    return urlp->protocol;
}

// e.g., "www.google.com"
const char *
url_parser_get_host (url_parser_t *urlp)
{
    return urlp->host;
}

// "/search"  (and if no path is specified, just "/")
const char *
url_parser_get_path (url_parser_t *urlp)
{
    return urlp->path;
}

// e.g. 8080
int
url_parser_get_port (url_parser_t *urlp)
{
    return urlp->port;
}

// returns null def if no parameter specified.
const char *
url_parser_get_parameter (url_parser_t *urlp, const char *key, const char *def)
{
    for (int i = 0; i < zarray_size(urlp->keys); i++) {
        char *thiskey;
        zarray_get(urlp->keys, i, &thiskey);
        if (!strcmp(thiskey, key)) {
            char *val;
            zarray_get(urlp->vals, i, &val);
            return val;
        }
    }

    return def;
}

// how many parameters were manually specified?
int
url_parser_num_parameters (url_parser_t *urlp)
{
    return zarray_size(urlp->keys);
}

// what was the name of the nth specified parameter?
const char *
url_parser_get_parameter_name (url_parser_t *urlp, int idx)
{
    char *v;
    zarray_get(urlp->keys, idx, &v);
    return v;
}

const char *
url_parser_get_parameter_value (url_parser_t *urlp, int idx)
{
    char *v;
    zarray_get(urlp->vals, idx, &v);
    return v;
}

struct testcase {
    char *url, *protocol, *hostport, *host, *path;
    int port;
};

void
url_parser_test (void)
{
    struct testcase testcases[] = {  { .url = "http://www.google.com",
                                       .protocol = "http://",
                                       .host = "www.google.com",
                                       .path = "/",
                                       .port = -1 },

                                     { .url = "http://www.google.com:8080/search?a=b&c=d",
                                       .protocol = "http://",
                                       .host = "www.google.com",
                                       .path = "/search",
                                       .port = 8080 },

                                     { .url = "file:///tmp/foobar",
                                       .protocol = "file://",
                                       .host = "",
                                       .path = "/tmp/foobar",
                                       .port = -1 },

                                     { .url = "pgusb://?fidx=4",
                                       .protocol = "pgusb://",
                                       .host = "",
                                       .path = "/",
                                       .port = -1 },

                                     { .url = "pgusb://287157?fidx=4&a=b",
                                       .protocol = "pgusb://",
                                       .host = "287157",
                                       .path = "/",
                                       .port = -1 },

                                     { .url = NULL }
    };

    for (int idx = 0; testcases[idx].url != NULL; idx++) {
        url_parser_t *urlp = url_parser_create(testcases[idx].url);

        printf("url:      %s\n", testcases[idx].url);
        printf("protocol: %s\n", urlp->protocol);
        printf("host:     %s\n", urlp->host);
        printf("port:     %d\n", urlp->port);
        printf("path:     %s\n", urlp->path);

        for (int i = 0; i < zarray_size(urlp->keys); i++) {
            char *key, *val;
            zarray_get(urlp->keys, i, &key);
            zarray_get(urlp->vals, i, &val);
            printf("  %s => %s\n", key, val);
        }

        assert(!strcmp(urlp->protocol, testcases[idx].protocol));
        assert(!strcmp(urlp->host, testcases[idx].host));
        assert(!strcmp(urlp->path, testcases[idx].path));
        assert(urlp->port == testcases[idx].port);

        printf("\n\n");

        url_parser_destroy(urlp);

    }
}
