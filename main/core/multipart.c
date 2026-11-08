#include "multipart.h"

#include <stdio.h>
#include <string.h>

// Appends `s` to out[*len], keeping it NUL-terminated. False if it doesn't fit.
static int add(char *out, size_t outsz, size_t *len, const char *s)
{
    size_t n = strlen(s);
    if (*len + n + 1 > outsz) return 0;
    memcpy(out + *len, s, n + 1);
    *len += n;
    return 1;
}

int multipart_head(char *out, size_t outsz, const char *boundary, const mp_field_t *fields, int nfields,
                   const char *file_field, const char *filename, const char *content_type)
{
    size_t len = 0;
    if (outsz == 0) return -1;
    out[0] = '\0';
    for (int i = 0; i < nfields; i++) {
        if (!(add(out, outsz, &len, "--") && add(out, outsz, &len, boundary) &&
              add(out, outsz, &len, "\r\nContent-Disposition: form-data; name=\"") &&
              add(out, outsz, &len, fields[i].name) && add(out, outsz, &len, "\"\r\n\r\n") &&
              add(out, outsz, &len, fields[i].value) && add(out, outsz, &len, "\r\n")))
            return -1;
    }
    if (!(add(out, outsz, &len, "--") && add(out, outsz, &len, boundary) &&
          add(out, outsz, &len, "\r\nContent-Disposition: form-data; name=\"") && add(out, outsz, &len, file_field) &&
          add(out, outsz, &len, "\"; filename=\"") && add(out, outsz, &len, filename) &&
          add(out, outsz, &len, "\"\r\nContent-Type: ") && add(out, outsz, &len, content_type) &&
          add(out, outsz, &len, "\r\n\r\n")))
        return -1;
    return (int)len;
}

int multipart_tail(char *out, size_t outsz, const char *boundary)
{
    size_t len = 0;
    if (outsz == 0) return -1;
    out[0] = '\0';
    if (!(add(out, outsz, &len, "\r\n--") && add(out, outsz, &len, boundary) && add(out, outsz, &len, "--\r\n")))
        return -1;
    return (int)len;
}
