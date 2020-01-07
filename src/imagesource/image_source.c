#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>

#include "common/string_util.h"
#include "common/url_parser.h"

#include "image_source.h"

void
image_source_print_features (image_source_t *isrc);

image_source_t *
image_source_v4l2_open (url_parser_t *urlp);
image_source_t *
image_source_dc1394_open (url_parser_t *urlp);
image_source_t *
image_source_islog_open (url_parser_t *urlp);
image_source_t *
image_source_pgusb_open (url_parser_t *urlp);
image_source_t *
image_source_filedir_open (url_parser_t *urlp);
image_source_t *
image_source_tcp_open (url_parser_t *urlp);
image_source_t *
image_source_null_open (url_parser_t *urlp);

void
image_source_enumerate_v4l2 (zarray_t *urls);
void
image_source_enumerate_dc1394 (zarray_t *urls);
void
image_source_enumerate_pgusb (zarray_t *urls);
void
image_source_enumerate_null (zarray_t *urls);


image_source_t *
image_source_open (const char *url)
{
    image_source_t *isrc = NULL;

    // get feature key/value pairs
    url_parser_t *urlp = url_parser_create (url);
    if (urlp == NULL) // bad URL format
        return NULL;

    const char *protocol = url_parser_get_protocol (urlp);

    if (0==strcmp (protocol, "v4l2://"))
        isrc = image_source_v4l2_open(urlp);
    else if (0==strcmp (protocol, "dc1394://"))
        isrc = image_source_dc1394_open (urlp);
    else if (0==strcmp (protocol, "islog://"))
        isrc = image_source_islog_open (urlp);
    else if (0==strcmp(protocol, "pgusb://"))
        isrc = image_source_pgusb_open (urlp);
    else if (0==strcmp(protocol, "file://"))
        isrc = image_source_filedir_open (urlp);
    else if (0==strcmp(protocol, "dir://"))
        isrc = image_source_filedir_open (urlp);
    else if (0==strcmp(protocol, "tcp://"))
        isrc = image_source_tcp_open (urlp);
    else if (0==strcmp(protocol, "null://"))
        isrc = image_source_null_open (urlp);

    // handle parameters
    if (isrc != NULL) {
        int found[url_parser_num_parameters(urlp)];

        for (int param_idx = 0; param_idx < url_parser_num_parameters (urlp); param_idx++) {
            const char *key = url_parser_get_parameter_name (urlp, param_idx);
            const char *value = url_parser_get_parameter_value (urlp, param_idx);

            if (0==strcmp (key, "fidx")) {
                printf ("image_source.c: set feature %30s = %15s\n", key, value);
                int fidx = atoi (url_parser_get_parameter (urlp, "fidx", "0"));
                printf ("SETTING fidx %d\n", fidx);
                isrc->set_format (isrc, fidx);
                found[param_idx] = 1;
                continue;
            }

            if (0==strcmp (key, "format")) {
                printf ("image_source.c: set feature %30s = %15s\n", key, value);
                isrc->set_named_format (isrc, value);
                found[param_idx] = 1;
                continue;
            }

            if (0==strcmp (key, "print")) {
                image_source_print_features (isrc);
                continue;
            }

            // pass through a device-specific parameter.
            for (int feature_idx = 0; feature_idx < isrc->num_features (isrc); feature_idx++) {

                if (0==strcmp (isrc->get_feature_name(isrc, feature_idx), key)) {
                    char *endptr = NULL;
                    double dv = strtod (value, &endptr);
                    if (endptr != value + strlen (value)) {
                        printf ("Parameter for key '%s' is invalid. Must be a number.\n",
                               isrc->get_feature_name (isrc, feature_idx));
                        goto cleanup;
                    }

                    int res = isrc->set_feature_value (isrc, feature_idx, dv);
                    if (res != 0)
                        printf ("Error setting feature: key %s value %s, error code %d\n",
                                key, value, res);

                    double setvalue = isrc->get_feature_value (isrc, feature_idx);
                    printf("image_source.c: set feature %30s = %15s (double %12.6f). Actually set to %8.3f\n",
                           key, value, dv, setvalue);

                    found[param_idx] = 1;
                    break;
                }
            }
        }

        for (int param_idx = 0; param_idx < url_parser_num_parameters (urlp); param_idx++) {
            if (found[param_idx] != 1) {
                const char *key = url_parser_get_parameter_name (urlp, param_idx);
                const char *value = url_parser_get_parameter_value (urlp, param_idx);

                printf ("Parameter not found. Key: %s Value: %s\n", key, value);
            }
        }
    }

cleanup:
    url_parser_destroy (urlp);

    return isrc;
}

zarray_t *
image_source_enumerate (void)
{
    zarray_t *urls = zarray_create (sizeof(char*));

    image_source_enumerate_v4l2 (urls);
    image_source_enumerate_pgusb (urls);
    image_source_enumerate_dc1394 (urls);
    image_source_enumerate_null (urls);

    return urls;
}

void
image_source_enumerate_free (zarray_t *urls)
{
    if (urls == NULL)
        return;

    for (int i = 0; zarray_size (urls); i++) {
        char *url;
        zarray_get (urls, i, &url);
        free (url);
    }

    zarray_destroy (urls);
}

void
image_source_print_features (image_source_t *isrc)
{
    printf ("Features:\n");

    int n = isrc->num_features (isrc);
    for (int i = 0; i < n; i++) {

        const char *name = isrc->get_feature_name (isrc, i);
        const char *type = isrc->get_feature_type (isrc, i);
        double value = isrc->get_feature_value (isrc, i);

        printf ("    %-30s : ", name);

        if (type[0] == 'b')
            printf ("Boolean %20s", ((int) value) ? "True" : "False");
        else if (type[0] == 'i') {

            zarray_t *tokens = str_split (type, ",");

            if (zarray_size (tokens) == 3 || zarray_size (tokens) == 4) {
                char *min = NULL, *max = NULL;
                zarray_get (tokens, 1, min);
                zarray_get (tokens, 2, max);
                char *inc = "1";
                if (zarray_size (tokens) == 4)
                    zarray_get (tokens, 3, inc);

                printf ("Int     %20i Min %20s Max %20s Inc %20s",
                        (int) value, min, max, inc);
            }

            zarray_map (tokens, free);
            zarray_destroy (tokens);

        }
        else if (type[0] == 'f') {

            zarray_t *tokens = str_split (type, ",");

            if (zarray_size (tokens) == 3 || zarray_size (tokens) == 4) {
                char *min = NULL, *max = NULL;
                zarray_get (tokens, 1, min);
                zarray_get (tokens, 2, max);
                char *inc = NULL;
                if (zarray_size (tokens) == 4)
                    zarray_get (tokens, 3, inc);

                printf ("Float   %20.15f Min %20s Max %20s", value, min, max);
                if (inc)
                    printf ("Inc %20s", inc);
            }

            zarray_map (tokens, free);
            zarray_destroy (tokens);

        }
        else if (type[0] == 'c') {

            zarray_t *tokens = str_split (type, ",");

            printf ("Enum    %20i (", (int) value);

            char *c = NULL;
            for (int i = 1; i < zarray_size (tokens); i++) {
                zarray_get (tokens, i, c);
                printf ("%s%s", c, (i+1 == zarray_size(tokens)) ? "" : ", ");
            }

            printf (")");

            zarray_map (tokens, free);
            zarray_destroy (tokens);
        }

        printf ("\n");
    }
}

