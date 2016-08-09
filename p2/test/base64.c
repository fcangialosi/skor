#include <glib.h>
#include <stdio.h>

int main (int argc, char *argv[]) {
	size_t foo = 3294967295;
	gchar *encoded = g_base64_encode((guchar *)&foo, sizeof(foo));
	printf("encoded: %s\n", encoded);

	gsize size;
	guchar *decoded = g_base64_decode(encoded, &size);

	printf("Decoded: size %lu, %lu = %lu\n", size, foo, *((size_t *)decoded));

	g_free(encoded);
	g_free(decoded);

	return 0;
}
