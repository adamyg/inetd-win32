

void
w32_wbuf_init(WIDEBuffer_t *wbuf)
{
    wbuf->result = NULL;
    wbuf->length = 0;
}


void
w32_wbuf_destroy(WIDEBuffer_t *wbuf)
{
    if (wbuf->length && wbuf->result) {
        assert(wbuf->length > _countof(wbuf->_buffer));
        if (wbuf->result != wbuf->_buffer) {
            free(wbuf->result);
        }
    }
    wbuf->result = NULL;
    wbuf->length = 0;
}


/*
 *  Convert the path from ansi to unicode, to support paths longer than MAX_PATH
 *
 *
 * This file is part of inetd-win32.
 *
 * The applications are free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * Redistributions of source code must retain the above copyright
 * notice, and must be distributed with the license document above.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, and must include the license document above in
 * the documentation and/or other materials provided with the
 * distribution.
 *
 * This project is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * license for more details.
 * ==end==
 */
int
w32_utf8_to_wide(const char *path, WIDEBuffer_t *wbuf)
{
    const UINT codePage = CP_UTF8;
    int len;

    // Empty string

    assert(0 == wbuf->length);
    assert(NULL == wbuf->result);

    if (!*path) {
        wbuf->_buffer[0] = 0;
        wbuf->result = wbuf->_buffer;
        return 0;
    }

    // Determine working storage requirements

    if (0 == (len = MultiByteToWideChar(codePage, /*flags*/0, path, -1, 0, 0))) {
        /* Note:
         *  dwFlags:    MB_ERR_INVALID_CHARS
         *      Fail if an invalid input character is encountered.
         *      Starting with Windows Vista, the function does not drop illegal code points if the application does not set this flag, 
         *      but instead replaces illegal sequences with U+FFFD (encoded as appropriate for the specified codepage).
         *
         *    Windows 2000 with SP4 and later, Windows XP:
         *      If this flag is not set, the function silently drops illegal code points. A call to GetLastError returns ERROR_NO_UNICODE_TRANSLATION.
         */
        errno = EINVAL;
        return -1;
    }

    // Decode

    if (len <= _countof(wpath->_buffer)) {      // internal storage
        if (0 == MultiByteToWideChar(codePage, 0, path, -1, wbuf->_buffer, _countof(wpath->_buffer))) {
            errno = EINVAL;
            return -1;
        }

        wbuf->result = wbuf->_buffer;

    } else {                                    // dynamic storage
        wchar_t *widePath = 0;

        if (NULL == (wpath = (wchar_t *) malloc(len * sizeof(wchar_t)))) {
            return -1;
        }

        if (0 == MultiByteToWideChar(codePage, 0, path, -1, wpath, len)) {
            errno = EINVAL;
            free(wpath);
            return -1;
        }

        wbuf->result = wpath;
    }

    wbuf->length = len;
    return len;
}
