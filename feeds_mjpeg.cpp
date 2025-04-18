// (C) 2025 by folkert van heusden, released under the MIT license
#include <assert.h>
#include <string>
#include <cstring>
#include <turbojpeg.h>
#include <unistd.h>
#include <curl/curl.h>

#include "error.h"
#include "feeds.h"


tjhandle jpegDecompressor { tjInitDecompress() };

bool read_JPEG_memory(unsigned char *in, int n_bytes_in, int *w, int *h, unsigned char **pixels)
{
	bool ok = true;

	int jpeg_subsamp = 0;
	if (tjDecompressHeader2(jpegDecompressor, in, n_bytes_in, w, h, &jpeg_subsamp) == -1)
		return false;

	*pixels = (unsigned char *)malloc(*w * *h * 3);
	if (tjDecompress2(jpegDecompressor, in, n_bytes_in, *pixels, *w, 0/*pitch*/, *h, TJPF_RGB, TJFLAG_FASTDCT) == -1) {
		free(*pixels);
		*pixels = nullptr;
		ok = false;
	}

	return ok;
}

typedef struct
{
	uint8_t *data;
	size_t   len;
	char    *boundary;
} work_header_t;

static size_t write_headers(void *ptr, size_t size, size_t nmemb, void *mypt)
{
	work_header_t *pctx = reinterpret_cast<work_header_t *>(mypt);
	size_t         n    = size * nmemb;
	size_t         tl   = pctx -> len + n;

	pctx -> data = reinterpret_cast<uint8_t *>(realloc(pctx -> data, tl + 1));
	if (!pctx -> data)
		error_exit(true, "HTTP headers: cannot allocate %zu bytes of memory", tl);

	memcpy(&pctx -> data[pctx -> len], ptr, n);
	pctx -> len += n;
	pctx -> data[pctx -> len] = 0x00;

	// 512kB header limit
	if (pctx -> len >= 512 * 1024) {
		printf("headers too large\n");
		return 0;
	}

	char *p           = reinterpret_cast<char *>(pctx -> data);
	char *em          = NULL;
	char *ContentType = strcasestr(p, "Content-Type:");
	if (ContentType) {
		em = strstr(ContentType, "\r\n");
		if (!em)
			em = strstr(ContentType, "\n");

		if (em != nullptr) {
			char *temp = strndup(ContentType, em - ContentType);

			char *bs = strchr(temp, '=');
			if (bs) {
				free(pctx -> boundary);

				if (bs[1] == '"')
					pctx -> boundary = strdup(bs + 2);
				else
					pctx -> boundary = strdup(bs + 1);

				char *dq = strchr(pctx -> boundary, '"');
				if (dq)
					*dq = 0x00;
			}

			free(temp);
		}
	}

	return n;
}

typedef struct
{
	work_header_t *headers;
	container     *c;
	bool           first;
	bool           header;
	uint8_t       *data;
	size_t         n;
	size_t         req_len;
} work_data_t;

static int xfer_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	return 0;
}

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *mypt)
{
	work_data_t *w         = (work_data_t *)mypt;
	const size_t full_size = size * nmemb;

	w -> data = (uint8_t *)realloc(w -> data, w -> n + full_size + 1);
	memcpy(&w -> data[w -> n], ptr, full_size);
	w -> n += full_size;
	w -> data[w -> n] = 0x00;

	if (w -> n >= 32 * 1024 * 1024) { // sanity limit
		printf("frame too big\n");
		return 0;
	}

process:
	if (w -> header) {
		bool proper_header = true;

		char *header_end = strstr((char *)w -> data, "\r\n\r\n");
		if (!header_end) {
			header_end = strstr((char *)w -> data, "\n\n");
			if (!header_end)
				return full_size;

			proper_header = false;
		}

		*header_end = 0x0;

		char *cl = strcasestr((char *)w -> data, "Content-Length:");
		if (!cl && w -> headers -> boundary == NULL) {
			//printf("====> %s\n",w->data);
			if (w -> headers -> boundary)
				goto with_boundary;
			return 0;
		}

		if (cl)
			w -> req_len = atoi(&cl[15]);
		else
			w -> req_len = 0;

		w -> header = false;

		size_t left = w -> n - (strlen((char *)w -> data) + (proper_header?4:2));
		if (left) {
			// printf("LEFT %zu\n", left);
			memmove(w -> data, header_end + (proper_header?4:2), left);
		}
		w -> n = left;
	}
	else if (w -> n >= w -> req_len && w -> req_len) {
		//printf("frame! (%p %zu/%zu)\n", w -> data, w -> n, w -> req_len);

		int            dw   = 0;
		int            dh   = 0;
		unsigned char *temp = NULL;
		if (read_JPEG_memory(w->data, w->req_len, &dw, &dh, &temp)) {
			w->c->set_pixels(temp, dw, dh);
			free(temp);
		}

		size_t left = w -> n - w -> req_len;
		if (left) {
			memmove(w -> data, &w -> data[w -> req_len], left);
		//printf("LEFT %zu\n", left);
		}
		w -> n = left;

		w -> req_len = 0;

		w -> header = true;
	}
	// for broken cameras that don't include a content-length in their headers
	else if (w -> headers -> boundary && w -> req_len == 0) {
with_boundary:
		ssize_t bl = strlen(w -> headers -> boundary);

		for(ssize_t i=0; i<w -> n - bl; i++) {
			if (memcmp(&w -> data[i], w -> headers -> boundary, bl) == 0) {
				char *em = strstr((char *)&w -> data[i], "\r\n\r\n");

				if (em) {
					w -> req_len = i;
					// printf("detected marker: %s\n", (char *)&w -> data[i]);
					goto process;
				}
			}
		}
	}

	return full_size;
}

mjpeg_feed::mjpeg_feed(const std::string & url, container *const c): feed(c), url(url)
{
	th = new std::thread(std::ref(*this));
}

mjpeg_feed::~mjpeg_feed()
{
}

void mjpeg_feed::operator()()
{
	constexpr const int timeout = 5000;

	for(;;)
	{
		CURL *ch = curl_easy_init();

		char error[CURL_ERROR_SIZE] = "?";
		if (curl_easy_setopt(ch, CURLOPT_ERRORBUFFER, error))
			error_exit(false, "curl_easy_setopt(CURLOPT_ERRORBUFFER) failed: %s", error);

		curl_easy_setopt(ch, CURLOPT_URL, url.c_str());

		if (curl_easy_setopt(ch, CURLOPT_TCP_KEEPALIVE, 1L))
			error_exit(false, "curl_easy_setopt(CURLOPT_TCP_KEEPALIVE) failed: %s", error);
	 
		if (curl_easy_setopt(ch, CURLOPT_TCP_KEEPIDLE, 120L))
			error_exit(false, "curl_easy_setopt(CURLOPT_TCP_KEEPIDLE) failed: %s", error);
	 
		if (curl_easy_setopt(ch, CURLOPT_TCP_KEEPINTVL, 60L))
			error_exit(false, "curl_easy_setopt(CURLOPT_TCP_KEEPINTVL) failed: %s", error);

		std::string useragent = "InfoViewer";

		if (curl_easy_setopt(ch, CURLOPT_USERAGENT, useragent.c_str()))
			error_exit(false, "curl_easy_setopt(CURLOPT_USERAGENT) failed: %s", error);

		if (true) {  // ignore certificate errors (this is not security software)
			if (curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0))
				error_exit(false, "curl_easy_setopt(CURLOPT_SSL_VERIFYPEER) failed: %s", error);

			if (curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 0))
				error_exit(false, "curl_easy_setopt(CURLOPT_SSL_VERIFYHOST) failed: %s", error);
		}

		// abort if slower than 5 bytes/sec during "60 + 2 * timeout" seconds
		curl_easy_setopt(ch, CURLOPT_LOW_SPEED_TIME, 60L + timeout * 2);
		curl_easy_setopt(ch, CURLOPT_LOW_SPEED_LIMIT, 5L);

		work_header_t wh { };
		curl_easy_setopt(ch, CURLOPT_HEADERDATA, &wh);
		curl_easy_setopt(ch, CURLOPT_HEADERFUNCTION, write_headers);

		curl_easy_setopt(ch, CURLOPT_VERBOSE, 1);

		curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, write_data);

		curl_easy_setopt(ch, CURLOPT_TCP_FASTOPEN, 1);

		work_data_t *w = new work_data_t;
		w -> c = c;
		w -> first = w -> header = true;
		w -> data = NULL;
		w -> n = 0;
		w -> headers = &wh;
		curl_easy_setopt(ch, CURLOPT_WRITEDATA, w);

		curl_easy_setopt(ch, CURLOPT_XFERINFODATA, w);
		curl_easy_setopt(ch, CURLOPT_XFERINFOFUNCTION, xfer_callback);
		curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 0L);

		CURLcode rc = CURLE_OK;
		if ((rc = curl_easy_perform(ch)) != CURLE_OK) {
		}

		free(w -> data);
		delete w;

		free(wh.boundary);
		free(wh.data);

		long http_code = 0;
		if (curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &http_code))
			printf("CURL(CURLINFO_RESPONSE_CODE) error: %s\n", curl_easy_strerror(rc));

		if (http_code == 200) {
			// all fine
		}
		else if (http_code == 401)
			printf("HTTP: Not authenticated\n");
		else if (http_code == 404)
			printf("HTTP: URL not found\n");
		else if (http_code >= 500 && http_code <= 599)
			printf("HTTP: Server error\n");
		else {
			printf("HTTP error %ld\n", http_code);
		}

		curl_easy_cleanup(ch);

		usleep(101000);
	}
}
