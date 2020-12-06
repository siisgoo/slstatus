/* See LICENSE file for copyright and license details. */
#include <ifaddrs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../util.h"

#define RSSI_TO_PERC(rssi) \
			rssi >= -50 ? 100 : \
			(rssi <= -100 ? 0 : \
			(2 * (rssi + 100)))

#if defined(__linux__)
	#include <limits.h>
	#include <linux/wireless.h>

	const char *
	wifi_conn(const char *interface)
	{
		int conn_status;
		char path[PATH_MAX];
		FILE *fp;
		char *p;
		char status[5];

		conn_status = esnprintf(path, sizeof(path), "/sys/class/net/%s/operstate", interface);

		if (!(fp = fopen(path, "r"))) {
			warn("fopen '%s':", path);
			return bprintf("OFF");
		}
		p = fgets(status, 5, fp);
		fclose(fp);
		if (!p || strcmp(status, "up\n") != 0) {
			return bprintf("OFF");
		}

		if(conn_status > 0)
			return bprintf("");
		else
			return bprintf("D");
	}

	const void *
	wifi_perc(const char *interface)
	{
		int cur;
		size_t i;
		char *p, *datastart;
		char path[PATH_MAX];
		char status[5];
		FILE *fp;

		if (esnprintf(path, sizeof(path), "/sys/class/net/%s/operstate",
		              interface) < 0) {
			return NULL;
		}
		if (!(fp = fopen(path, "r"))) {
			warn("fopen '%s':", path);
			return NULL;
		}
		p = fgets(status, 5, fp);
		fclose(fp);
		if (!p || strcmp(status, "up\n") != 0) {
			return NULL;
		}

		if (!(fp = fopen("/proc/net/wireless", "r"))) {
			warn("fopen '/proc/net/wireless':");
			return NULL;
		}

		for (i = 0; i < 3; i++) {
			if (!(p = fgets(buf, sizeof(buf) - 1, fp)))
				break;
		}
		fclose(fp);
		if (i < 2 || !p) {
			return NULL;
		}

		if (!(datastart = strstr(buf, interface))) {
			return NULL;
		}

		datastart = (datastart+(strlen(interface)+1));
		sscanf(datastart + 1, " %*d   %d  %*d  %*d\t\t  %*d\t   "
		       "%*d\t\t%*d\t\t %*d\t  %*d\t\t %*d", &cur);

		/* 70 is the max of /proc/net/wireless */
		return bprintf("%d", (int)((float)cur / 70 * 100));
	}

	const char *
	wifi_essid(const char *interface)
	{
		static char id[IW_ESSID_MAX_SIZE+1];
		int sockfd;
		struct iwreq wreq;

		memset(&wreq, 0, sizeof(struct iwreq));
		wreq.u.essid.length = IW_ESSID_MAX_SIZE+1;
		if (esnprintf(wreq.ifr_name, sizeof(wreq.ifr_name), "%s",
		              interface) < 0) {
			return NULL;
		}

		if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			warn("socket 'AF_INET':");
			return NULL;
		}
		wreq.u.essid.pointer = id;
		if (ioctl(sockfd,SIOCGIWESSID, &wreq) < 0) {
			warn("ioctl 'SIOCGIWESSID':");
			close(sockfd);
			return NULL;
		}

		close(sockfd);

		if (!strcmp(id, "")) {
			return NULL;
		}

		return id;
	}
		
	const char *
	wifi_status2d(const char *interface)
	{
		int *perc;
		perc = NULL;

		if ((perc = ccToInt(wifi_perc(interface))) == NULL) {
			goto drawDicon;
		}

		int offset, x, y, w, barh, lh, mh, hh;
		x  = INDENT_WIDTH;
		w  = 3;
		lh = CENTRED / 3;
		y  = MAX_HEIGHT - INDENT_HEIGHT;
		mh = lh * 2;
		hh = lh * 3;
		barh = CENTRED;
		offset = 2 + w;

		char fg[7] = DEFAULT_FG_C; // intToHexColor(DEFAULT_FG, fg);

		char low[MAX_BAR_LEN], med[MAX_BAR_LEN], hig[MAX_BAR_LEN], all[MAX_BAR_LEN * 3];
		low[0] = med[0] = hig[0] = '\0';

		if (*perc > 75) {
			printBar(low,
					x, y-lh, w, lh, offset, lh, fg, DEFAULT_BG_C);
			printBar(med,
					x, y-mh, w, mh, offset, mh, fg, DEFAULT_BG_C);
			printBar(hig,
					x, y-hh, w, hh, offset, hh, fg, DEFAULT_BG_C);
		} else if (*perc <= 75 && *perc > 25) {
			printBar(low,
					x, y-lh, w, lh, offset, lh, fg, DEFAULT_BG_C);
			printBar(med,
					x, y-mh, w, mh, offset*2, mh, fg, DEFAULT_BG_C);

		} else if (*perc <= 25) {
			printBar(low,
					x, y-lh, w, lh, offset*3, lh, fg, DEFAULT_BG_C);
		} else {
			return NULL;
		}

		snprintf(all, sizeof(all),
				"%s%s%s", low, med, hig);

		return bprintf("%s", all);
drawDicon:
		return bprintf("^c#FF0000^D^d^");
	}
#elif defined(__OpenBSD__)
	#include <net/if.h>
	#include <net/if_media.h>
	#include <net80211/ieee80211.h>
	#include <sys/select.h> /* before <sys/ieee80211_ioctl.h> for NBBY */
	#include <net80211/ieee80211_ioctl.h>
	#include <stdlib.h>
	#include <sys/types.h>

	static int
	load_ieee80211_nodereq(const char *interface, struct ieee80211_nodereq *nr)
	{
		struct ieee80211_bssid bssid;
		int sockfd;
		uint8_t zero_bssid[IEEE80211_ADDR_LEN];

		memset(&bssid, 0, sizeof(bssid));
		memset(nr, 0, sizeof(struct ieee80211_nodereq));
		if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			warn("socket 'AF_INET':");
			return 0;
		}
		strlcpy(bssid.i_name, interface, sizeof(bssid.i_name));
		if ((ioctl(sockfd, SIOCG80211BSSID, &bssid)) < 0) {
			warn("ioctl 'SIOCG80211BSSID':");
			close(sockfd);
			return 0;
		}
		memset(&zero_bssid, 0, sizeof(zero_bssid));
		if (memcmp(bssid.i_bssid, zero_bssid,
		    IEEE80211_ADDR_LEN) == 0) {
			close(sockfd);
			return 0;
		}
		strlcpy(nr->nr_ifname, interface, sizeof(nr->nr_ifname));
		memcpy(&nr->nr_macaddr, bssid.i_bssid, sizeof(nr->nr_macaddr));
		if ((ioctl(sockfd, SIOCG80211NODE, nr)) < 0 && nr->nr_rssi) {
			warn("ioctl 'SIOCG80211NODE':");
			close(sockfd);
			return 0;
		}

		return close(sockfd), 1;
	}

	const char *
	wifi_perc(const char *interface)
	{
		struct ieee80211_nodereq nr;
		int q;

		if (load_ieee80211_nodereq(interface, &nr)) {
			if (nr.nr_max_rssi) {
				q = IEEE80211_NODEREQ_RSSI(&nr);
			} else {
				q = RSSI_TO_PERC(nr.nr_rssi);
			}
			return bprintf("%d", q);
		}

		return NULL;
	}

	const char *
	wifi_essid(const char *interface)
	{
		struct ieee80211_nodereq nr;

		if (load_ieee80211_nodereq(interface, &nr)) {
			return bprintf("%s", nr.nr_nwid);
		}

		return NULL;
	}
#elif defined(__FreeBSD__)
	#include <net/if.h>
	#include <net80211/ieee80211_ioctl.h>

	int
	load_ieee80211req(int sock, const char *interface, void *data, int type, size_t *len)
	{
		char warn_buf[256];
		struct ieee80211req ireq;
		memset(&ireq, 0, sizeof(ireq));
		ireq.i_type = type;
		ireq.i_data = (caddr_t) data;
		ireq.i_len = *len;

		strlcpy(ireq.i_name, interface, sizeof(ireq.i_name));
		if (ioctl(sock, SIOCG80211, &ireq) < 0) {
			snprintf(warn_buf,  sizeof(warn_buf),
					"ioctl: 'SIOCG80211': %d", type);
			warn(warn_buf);
			return 0;
		}

		*len = ireq.i_len;
		return 1;
	}

	const char *
	wifi_perc(const char *interface)
	{
		union {
			struct ieee80211req_sta_req sta;
			uint8_t buf[24 * 1024];
		} info;
		uint8_t bssid[IEEE80211_ADDR_LEN];
		int rssi_dbm;
		int sockfd;
		size_t len;
		const char *fmt;

		if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			warn("socket 'AF_INET':");
			return NULL;
		}

		/* Retreive MAC address of interface */
		len = IEEE80211_ADDR_LEN;
		fmt = NULL;
		if (load_ieee80211req(sockfd, interface, &bssid, IEEE80211_IOC_BSSID, &len))
		{
			/* Retrieve info on station with above BSSID */
			memset(&info, 0, sizeof(info));
			memcpy(info.sta.is_u.macaddr, bssid, sizeof(bssid));

			len = sizeof(info);
			if (load_ieee80211req(sockfd, interface, &info, IEEE80211_IOC_STA_INFO, &len)) {
				rssi_dbm = info.sta.info[0].isi_noise +
 					         info.sta.info[0].isi_rssi / 2;

				fmt = bprintf("%d", RSSI_TO_PERC(rssi_dbm));
			}
		}

		close(sockfd);
		return fmt;
	}

	const char *
	wifi_essid(const char *interface)
	{
		char ssid[IEEE80211_NWID_LEN + 1];
		size_t len;
		int sockfd;
		const char *fmt;

		if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			warn("socket 'AF_INET':");
			return NULL;
		}

		fmt = NULL;
		len = sizeof(ssid);
		memset(&ssid, 0, len);
		if (load_ieee80211req(sockfd, interface, &ssid, IEEE80211_IOC_SSID, &len )) {
			if (len < sizeof(ssid))
				len += 1;
			else
				len = sizeof(ssid);

			ssid[len - 1] = '\0';
			fmt = bprintf("%s", ssid);
		}

		close(sockfd);
		return fmt;
	}
#endif
