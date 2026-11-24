// SPDX-License-Identifier: GPL-2.0
/*
 * Capture one raw frame from the rear main S5K2L4 sensor on beyond2lte.
 *
 * Device nodes are located by name through /sys/class/video4linux so that
 * no media-controller enumeration (and hence no media.h) is needed:
 *   - the sensor subdev is named  "s5k2l4 <bus>-<addr>"
 *   - the receiver subdev is named "csis-<dev>"
 *   - the capture node is named   "exynos9820-csis capture"
 *
 * The CSIS video format has to match the active format on the sensor's
 * source pad, otherwise link validation fails with -EPIPE when streaming
 * starts, so the sensor and CSIS sink pads are programmed first.
 *
 * The frame is stored raw, statistics (mean/noise over the 10 significant
 * bits) and a SHA-256 of the stored bytes are printed.  The SHA-256 here is
 * a small FIPS 180-4 implementation so no extra dependency is required.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------------------------------------------------- SHA-256 ---- */

struct sha256_ctx {
	uint32_t state[8];
	uint64_t bits;
	uint8_t buf[64];
	size_t buflen;
};

static const uint32_t sha256_k[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(struct sha256_ctx *c, const uint8_t *p)
{
	uint32_t w[64], a, b, cc, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
		       ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
	for (i = 16; i < 64; i++) {
		uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);

		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	a = c->state[0]; b = c->state[1]; cc = c->state[2]; d = c->state[3];
	e = c->state[4]; f = c->state[5]; g = c->state[6]; h = c->state[7];

	for (i = 0; i < 64; i++) {
		uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
		uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);

		t1 = h + S1 + ch + sha256_k[i] + w[i];
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = cc; cc = b; b = a; a = t1 + t2;
	}

	c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
	c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}

static void sha256_init(struct sha256_ctx *c)
{
	c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
	c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
	c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
	c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
	c->bits = 0;
	c->buflen = 0;
}

static void sha256_update(struct sha256_ctx *c, const void *data, size_t len)
{
	const uint8_t *p = data;

	c->bits += (uint64_t)len * 8;
	while (len) {
		size_t n = 64 - c->buflen;

		if (n > len)
			n = len;
		memcpy(c->buf + c->buflen, p, n);
		c->buflen += n;
		p += n;
		len -= n;
		if (c->buflen == 64) {
			sha256_block(c, c->buf);
			c->buflen = 0;
		}
	}
}

static void sha256_final(struct sha256_ctx *c, uint8_t out[32])
{
	uint64_t bits = c->bits;
	uint8_t pad = 0x80;
	uint8_t zero = 0;
	uint8_t len[8];
	int i;

	sha256_update(c, &pad, 1);
	while (c->buflen != 56)
		sha256_update(c, &zero, 1);
	for (i = 0; i < 8; i++)
		len[i] = (uint8_t)(bits >> (56 - 8 * i));
	sha256_update(c, len, 8);
	for (i = 0; i < 8; i++) {
		out[i * 4] = (uint8_t)(c->state[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
		out[i * 4 + 3] = (uint8_t)c->state[i];
	}
}

/* ------------------------------------------------------- device lookup -- */

/*
 * Read /sys/class/video4linux/<node>/name for every node and return the
 * /dev/<node> path whose name contains @needle, or NULL when not found.
 */
static char *find_v4l_dev(const char *needle)
{
	DIR *d = opendir("/sys/class/video4linux");
	struct dirent *de;
	static char path[256];

	if (!d)
		return NULL;
	while ((de = readdir(d))) {
		char namefile[512], name[256];
		FILE *f;
		size_t len;

		if (de->d_name[0] == '.')
			continue;
		snprintf(namefile, sizeof(namefile),
			 "/sys/class/video4linux/%s/name", de->d_name);
		f = fopen(namefile, "r");
		if (!f)
			continue;
		len = fread(name, 1, sizeof(name) - 1, f);
		fclose(f);
		name[len] = '\0';
		if (name[len ? len - 1 : 0] == '\n')
			name[len - 1] = '\0';
		if (strstr(name, needle)) {
			closedir(d);
			snprintf(path, sizeof(path), "/dev/%s", de->d_name);
			return path;
		}
	}
	closedir(d);
	return NULL;
}

/* ------------------------------------------------------------- capture -- */

struct buffer {
	void *start;
	size_t length;
};

static int set_pad_format(int fd, unsigned int pad, unsigned int width,
			  unsigned int height, unsigned int code)
{
	struct v4l2_subdev_format fmt;
	int ret;

	memset(&fmt, 0, sizeof(fmt));
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.pad = pad;
	fmt.format.width = width;
	fmt.format.height = height;
	fmt.format.code = code;
	ret = ioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt);
	if (ret < 0)
		return ret;
	fprintf(stderr, "  pad %u -> %ux%u code %#x\n", pad, fmt.format.width,
		fmt.format.height, fmt.format.code);
	return 0;
}

int main(int argc, char **argv)
{
	const char *outfile = argc > 1 ? argv[1] : "/root/s5k2l4-frame.raw";
	unsigned int want_w = 4032, want_h = 3024;
	unsigned int bufcount = 4;

	if (argc > 3) {
		want_w = (unsigned int)strtoul(argv[2], NULL, 0);
		want_h = (unsigned int)strtoul(argv[3], NULL, 0);
		if (!want_w || !want_h) {
			fprintf(stderr, "bad size %s %s\n", argv[2], argv[3]);
			return 1;
		}
	}
	if (argc > 4)
		bufcount = (unsigned int)strtoul(argv[4], NULL, 0);
	if (bufcount < 2)
		bufcount = 2;
	fprintf(stderr, "requested %ux%u with %u buffers\n", want_w, want_h,
		bufcount);
	char *s5k_path, *csis_path, *vid_path;
	int s5k_fd = -1, csis_fd = -1, vid_fd = -1;
	struct v4l2_capability cap;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	struct buffer *bufs = NULL;
	struct sha256_ctx sha;
	uint8_t digest[32];
	unsigned char *data = NULL;
	size_t size = 0;
	double sum = 0.0, var = 0.0;
	unsigned int i;
	int n;
	int out_fd;
	ssize_t w;

	s5k_path = find_v4l_dev("s5k2l4 ");
	csis_path = find_v4l_dev("csis-");
	vid_path = find_v4l_dev("exynos9820-csis capture");

	fprintf(stderr, "sensor: %s\ncsis:   %s\ncapture:%s\n",
		s5k_path ? s5k_path : "(none)",
		csis_path ? csis_path : "(none)",
		vid_path ? vid_path : "(none)");
	if (!s5k_path || !csis_path || !vid_path)
		return 2;

	s5k_fd = open(s5k_path, O_RDWR);
	csis_fd = open(csis_path, O_RDWR);
	if (s5k_fd < 0 || csis_fd < 0) {
		fprintf(stderr, "open subdev: %s\n", strerror(errno));
		return 2;
	}

	fprintf(stderr, "programming pad formats:\n");
	if (set_pad_format(s5k_fd, 0, want_w, want_h,
			   MEDIA_BUS_FMT_SGRBG10_1X10) < 0) {
		fprintf(stderr, "sensor S_FMT: %s\n", strerror(errno));
		return 3;
	}
	if (set_pad_format(csis_fd, 0, want_w, want_h,
			   MEDIA_BUS_FMT_SGRBG10_1X10) < 0)
		fprintf(stderr, "csis S_FMT: %s (continuing)\n", strerror(errno));

	vid_fd = open(vid_path, O_RDWR | O_NONBLOCK);
	if (vid_fd < 0) {
		fprintf(stderr, "open %s: %s\n", vid_path, strerror(errno));
		return 4;
	}

	memset(&cap, 0, sizeof(cap));
	if (ioctl(vid_fd, VIDIOC_QUERYCAP, &cap) < 0) {
		fprintf(stderr, "QUERYCAP: %s\n", strerror(errno));
		return 5;
	}
	fprintf(stderr, "capture: %s (%s)\n", cap.card, cap.driver);

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = want_w;
	fmt.fmt.pix.height = want_h;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SGRBG10;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	if (ioctl(vid_fd, VIDIOC_S_FMT, &fmt) < 0) {
		fprintf(stderr, "S_FMT: %s\n", strerror(errno));
		return 6;
	}
	fprintf(stderr, "capture fmt %ux%u fourcc %.4s size %u bpl %u\n",
		fmt.fmt.pix.width, fmt.fmt.pix.height,
		(char *)&fmt.fmt.pix.pixelformat, fmt.fmt.pix.sizeimage,
		fmt.fmt.pix.bytesperline);

	memset(&req, 0, sizeof(req));
	req.count = bufcount;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(vid_fd, VIDIOC_REQBUFS, &req) < 0) {
		fprintf(stderr, "REQBUFS: %s\n", strerror(errno));
		return 7;
	}
	if (req.count < 2) {
		fprintf(stderr, "not enough buffers: %u\n", req.count);
		return 8;
	}

	bufs = calloc(req.count, sizeof(*bufs));
	if (!bufs)
		return 9;
	for (i = 0; i < req.count; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(vid_fd, VIDIOC_QUERYBUF, &buf) < 0) {
			fprintf(stderr, "QUERYBUF: %s\n", strerror(errno));
			return 10;
		}
		bufs[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
				     MAP_SHARED, vid_fd, buf.m.offset);
		if (bufs[i].start == MAP_FAILED) {
			fprintf(stderr, "mmap: %s\n", strerror(errno));
			return 11;
		}
		bufs[i].length = buf.length;
	}

	for (i = 0; i < req.count; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(vid_fd, VIDIOC_QBUF, &buf) < 0) {
			fprintf(stderr, "QBUF: %s\n", strerror(errno));
			return 12;
		}
	}

	{
		int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (ioctl(vid_fd, VIDIOC_STREAMON, &type) < 0) {
			fprintf(stderr, "STREAMON: %s\n", strerror(errno));
			return 13;
		}
	}
	fprintf(stderr, "streaming, waiting for a frame\n");

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	for (n = 0; n < 150; n++) {
		if (ioctl(vid_fd, VIDIOC_DQBUF, &buf) < 0) {
			if (errno == EAGAIN) {
				usleep(100000);
				continue;
			}
			fprintf(stderr, "DQBUF: %s\n", strerror(errno));
			return 14;
		}
		break;
	}
	if (n == 150) {
		fprintf(stderr, "no frame dequeued within 15 s\n");
		return 15;
	}

	size = buf.bytesused;
	if (size > bufs[buf.index].length)
		size = bufs[buf.index].length;
	data = malloc(size);
	if (!data)
		return 16;
	memcpy(data, bufs[buf.index].start, size);
	fprintf(stderr, "frame dequeued: index %u bytes %zu sequence %u\n",
		buf.index, size, buf.sequence);

	/* 10 bit samples, little endian, in 16 bit containers */
	{
		unsigned int nsamples = size / 2;
		double total = 0.0;
		unsigned int k;

		for (k = 0; k < nsamples; k++)
			total += (data[k * 2] | (data[k * 2 + 1] << 8)) & 0x3ff;
		sum = total / nsamples;
		for (k = 0; k < nsamples; k++) {
			double d = ((data[k * 2] | (data[k * 2 + 1] << 8)) & 0x3ff) - sum;

			var += d * d;
		}
		var /= nsamples;
		fprintf(stderr, "samples %u mean %.3f std %.3f\n", nsamples, sum,
			sqrt(var));
	}

	sha256_init(&sha);
	sha256_update(&sha, data, size);
	sha256_final(&sha, digest);

	out_fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (out_fd < 0) {
		fprintf(stderr, "open %s: %s\n", outfile, strerror(errno));
		return 17;
	}
	w = write(out_fd, data, size);
	close(out_fd);
	if (w < 0 || (size_t)w != size) {
		fprintf(stderr, "write: %s\n", strerror(errno));
		return 18;
	}

	printf("frame_bytes=%zu\n", size);
	printf("frame_mean_10bit=%.3f\n", sum);
	printf("frame_std_10bit=%.3f\n", sqrt(var));
	printf("sha256=");
	for (i = 0; i < 32; i++)
		printf("%02x", digest[i]);
	printf("\n");
	printf("file=%s\n", outfile);

	{
		int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		ioctl(vid_fd, VIDIOC_STREAMOFF, &type);
	}
	for (i = 0; i < req.count; i++)
		if (bufs[i].start && bufs[i].start != MAP_FAILED)
			munmap(bufs[i].start, bufs[i].length);
	free(bufs);
	free(data);
	close(vid_fd);
	close(s5k_fd);
	close(csis_fd);
	return 0;
}
