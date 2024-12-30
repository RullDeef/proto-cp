#pragma once

// creates new SSL certificate ~/.local/selecon/cert.pem if it does not exists
void cert_init(void);

// creates new SSL certificate ~/.local/selecon/cert.pem if it does not exists and returns its
// absolute path. Returned string does not need to be free'd
const char* cert_get_cert_path(void);

// creates new SSL certificate ~/.local/selecon/cert.pem if it does not exists and returns
// assosiated key absolute path. Returned string does not need to be free'd
const char* cert_get_key_path(void);
