#ifndef __URL_PARSER_H__
#define __URL_PARSER_H__

typedef struct url_parser url_parser_t;

#ifdef __cplusplus
extern "C" {
#endif

// In the examples below, consider the input:
// URL = http://www.google.com:8080/search?q=a

url_parser_t *
url_parser_create (const char *s);

void
url_parser_destroy (url_parser_t *urlp);

// e.g., "http://"
const char *
url_parser_get_protocol (url_parser_t *urlp);

// e.g., "www.google.com"
const char *
url_parser_get_host (url_parser_t *urlp);

// "/search"  (and if no path is specified, just "/")
const char *
url_parser_get_path (url_parser_t *urlp);

// e.g. 8080 (or -1 if no port specified)
int
url_parser_get_port (url_parser_t *urlp);

// returns null def if no parameter specified.
const char *
url_parser_get_parameter (url_parser_t *urlp, const char *key, const char *def);

// how many parameters were manually specified?
int
url_parser_num_parameters (url_parser_t *urlp);

// what was the name of the nth specified parameter?
const char *
url_parser_get_parameter_name (url_parser_t *urlp, int idx);

const char *
url_parser_get_parameter_value (url_parser_t *urlp, int idx);

#ifdef __cplusplus
}
#endif

#endif //__URL_PARSER_H__
