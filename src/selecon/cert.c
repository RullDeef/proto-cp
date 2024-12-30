#include "cert.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static bool cert_generated = false;
static char cert_path[256];
static char key_path[256];

void cert_init(void) {
	if (cert_generated)
		return;
	snprintf(key_path, sizeof(cert_path), "/home/%s/.local/share/selecon/key.pem", getlogin());
	snprintf(cert_path, sizeof(cert_path), "/home/%s/.local/share/selecon/cert.pem", getlogin());
	// check certificate exists
	if (access(cert_path, F_OK) == 0 && access(key_path, F_OK) == 0) {
		cert_generated = true;
		return;
	}
	char cmd[1024];
	snprintf(cmd,
	         sizeof(cmd),
	         "mkdir -p /home/%s/.local/share/selecon && openssl req -x509 -newkey rsa:4096 "
	         "-keyout %s -out %s -sha256 -days "
	         "3650 -nodes -subj "
	         "\"/C=XX/ST=StateName/L=CityName/O=CompanyName/OU=CompanySectionName/"
	         "CN=CommonNameOrHostname\"",
	         getlogin(),
	         key_path,
	         cert_path);
	if (system(cmd) != 0)
		fprintf(stderr, "failed to execute certificate creation command\n");
	else
		cert_generated = true;
}

const char* cert_get_cert_path(void) {
	cert_init();
	return cert_path;
}

const char* cert_get_key_path(void) {
	cert_init();
	return key_path;
}
