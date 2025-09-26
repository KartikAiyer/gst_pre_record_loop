#include <gst/gst.h>
#include <stdio.h>

int main(void) {
    gst_init(NULL, NULL);
    if (!gst_version_string()) {
        fprintf(stderr, "Failed to init GStreamer version string.\n");
        return 1;
    }
    printf("placeholder unit test executed\n");
    return 0;
}
