/********************************************************************
 *                                                                  *
 *  Copyright (C) 2007, 2008                                        *
 *  Charles Darwin University, Darwin, Australia                    *
 *                                                                  *
 *  This program is free software; you can redistribute it and/or   *
 *  modify it under the terms of the GNU General Public License as  *
 *  published by the Free Software Foundation; either version 2 of  *
 *  the License, or (at your option) any later version.             *
 *                                                                  *
 *  This program is distributed in the hope that it will be         *
 *  useful, but WITHOUT ANY WARRANTY; without even the implied      *
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR         *
 *  PURPOSE.  See the GNU General Public License for more details.  *
 *                                                                  *
 *  You should have received a copy of the GNU General Public       *
 *  License along with this program; if not, write to the Free      *
 *  Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,  *
 *  MA  02111-1307  USA                                             *
 *                                                                  *
 ********************************************************************
 *                                                                  *
 *  merge multiple PDS files into one file                          *
 *                                                                  *
 *  12/12/2007  S. W. Maier    start of work                        *
 *  12/12/2007  S. W. Maier    initial version                      *
 *  25/12/2007  S. W. Maier    added start and end date parameters  *
 *  10/06/2008  S. W. Maier    increased data buffer and test for   *
 *                             packet size before reading           *
 *  10/10/2008  S. W. Maier    increased data buffer                *
 *                                                                  *
 ********************************************************************
 *                                                                  *
 *  usage: pdsmerge start_date end_date APID <input 1> [<input 2>   *
 *         [...]] output                                            *
 *                                                                  *
 ********************************************************************
 *                                                                  *
 *  to do:                                                          *
 *   - support other sensors                                        *
 *                                                                  *
 *  known issues:                                                   *
 *   - doesn't take into account leap seconds                       *
 *                                                                  *
 ********************************************************************
 *                                                                  *
 *  build: cc pdsmerge.c -lm -o pdsmerge                            *
 *                                                                  *
 ********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>


/********************************************************************
 *                                                                  *
 *  defines                                                         *
 *                                                                  *
 ********************************************************************/
/* name */
#define NAME "pdsmerge"
/* version */
#define VERSION 1
/* revision */
#define REVISION 3
/* usage */
#define USAGE "start_date end_date APID <input 1> [<input 2> [...]] output\nstart_date/end_date: YYYY/MM/DD,hh:mm:ss or -"
/* primary header size */
#define PRI_HDR_SIZE 6
/* MODIS secondary header size */
#define MODIS_HDR_SIZE 12
/* data buffer size */
#define DATA_SIZE 100000
/* Julian Day of MODIS reference date (01/01/1958)*/
#define MODIS_REF_DATE 2436205.0


/********************************************************************
 *                                                                  *
 *  structure definitions                                           *
 *                                                                  *
 ********************************************************************/
/* primary header */
struct pri_hdr {
	int flag;
	int version;
	int type;
	int sec_hdr_flag;
	int apid;
	int seq_flags;
	int pkt_count;
	int pkt_length;
};

/* MODIS header */
struct modis_hdr {
	int days;
	unsigned long int millisec;
	int microsec;
	int ql;
	int pkt_type;
	int scan_count;
	int mirror_side;
	int src1;
	int src2;
	int conf;
	int sci_state;
	int sci_abnorm;
	int checksum;
};


/********************************************************************
 *                                                                  *
 *  function declarations                                           *
 *                                                                  *
 ********************************************************************/
int ReadPriHdr(FILE *f, unsigned char *buf);
int WritePriHdr(FILE *f, unsigned char *buf);
int DecodePriHdr(unsigned char *buf, struct pri_hdr *hdr);
int DecodeMODISHdr(unsigned char *buf, int len,
									 struct modis_hdr *hdr);
void julday(int minute, int hour, int day, int month, int year,
						double *jul);
void caldat(int *minute, int *hour, int *day, int *month, int *year,
						double jul);
int CalcChecksum12(unsigned char *buf, int n);


/********************************************************************
 *                                                                  *
 *  main function                                                   *
 *                                                                  *
 ********************************************************************/
int main(int argc, char *argv[]) {
	/* pointer to input file pointer array */
  FILE **fin;
	/* output file pointer */
	FILE *fout;
	/* pointer to primary header structure pointer array */
	struct pri_hdr **hdr;
	/* pointer to MODIS header structure pointer array */
	struct modis_hdr **mhdr;
	/* pointer to header buffer pointer array */
	unsigned char **buf_hdr;
	/* pointer to data buffer pointer array */
	unsigned char **buf_data;
	/* number of input files */
	int n;
	/* APID */
	int apid;
	/* checksum */
	int chksum;
	/* input stream with oldest packet */
	int oldest;
	/* flag for valid packet available */
	int valpkt;
	/* counter */
	int i;
	/* error code */
	int error;
	/* date/time */
	int year, month, day, hour, min, sec;
	/* start date/time */
	int startday;
	unsigned long startmillisec;
	/* end date/time */
	int endday;
	unsigned long endmillisec;
	/* buffer */
	double x;
	/* last packet date/time/pktcount */
	int lastdays = 0, lastmicrosec = 0, lastpktcount = 0;
	unsigned long int lastmillisec = 0;
	/* packet difference */
	int pktdiff;


	/* print version information */
	fprintf(stderr, "%s V%d.%d ("__DATE__")\n",
					NAME,	VERSION, REVISION);
	
	/* check number of arguments */
	if(argc < 6) {
		fprintf(stderr, "USAGE: %s %s\n", NAME, USAGE);
		return(20);
	}
	
	/* determine number of input files */
	n = argc - 5;

	/* get start date */
	if(strcmp(argv[1], "-") == 0) {
		startday = 0;
		startmillisec = 0;
	} else {
		if(sscanf(argv[1], " %d/%d/%d,%d:%d:%d",
						 &year, &month, &day, &hour, &min, &sec) != 6) {
			fprintf(stderr, "USAGE: %s %s\n", NAME, USAGE);
			return(20);
		}
		if((year < 1958) ||
			 (month < 1) || (month > 12) ||
			 (day < 1) || (day > 31) ||
			 (hour < 0) || (hour > 23) ||
			 (min < 0) || (min > 59) ||
			 (sec < 0) || (sec >59)) {
			fprintf(stderr, "USAGE: %s %s\n", NAME, USAGE);
			return(10);
		}
		julday(0, 0, day, month, year, &x);
		startday = (int)(x - MODIS_REF_DATE);
		startmillisec =
			(unsigned long)hour * 60L * 60L * 1000L +
			(unsigned long)min * 60L * 1000L +
			(unsigned long)sec * 1000L;
	}

	/* get end date */
	if(strcmp(argv[2], "-") == 0) {
		endday = 4000000;
		endmillisec = 90000000L;
	} else {
		if(sscanf(argv[2], " %d/%d/%d,%d:%d:%d",
						 &year, &month, &day, &hour, &min, &sec) != 6) {
			fprintf(stderr, "USAGE: %s %s\n", NAME, USAGE);
			return(20);
		}
		if((year < 1958) ||
			 (month < 1) || (month > 12) ||
			 (day < 1) || (day > 31) ||
			 (hour < 0) || (hour > 23) ||
			 (min < 0) || (min > 59) ||
			 (sec < 0) || (sec >59)) {
			fprintf(stderr, "USAGE: %s %s\n", NAME, USAGE);
			return(10);
		}
		julday(0, 0, day, month, year, &x);
		endday = (int)(x - MODIS_REF_DATE);
		endmillisec =
			(unsigned long)hour * 60L * 60L * 1000L +
			(unsigned long)min * 60L * 1000L +
			(unsigned long)sec * 1000L;
	}
	
	/* check if end date/time is after start date/time */
	if((endday < startday) ||
		 ((endday == startday) && (endmillisec <= startmillisec))) {
		fprintf(stderr, "USAGE: %s %s\n", NAME, USAGE);
		return(10);
	}
	
	/* get APID */
	apid = atoi(argv[3]);
	if((apid < 64) || (apid > 127)) {
		fprintf(stderr, "only APID 64 to 127 supported\n");
		return(10);
	}

	/* allocate pointer memory */
	if(!(fin = malloc(sizeof(FILE *) * n))) {
		fprintf(stderr, "not enough memory\n");
		return(10);
	}
	if(!(hdr = malloc(sizeof(struct pri_hdr *) * n))) {
		fprintf(stderr, "not enough memory\n");
		return(10);
	}
	if(!(mhdr = malloc(sizeof(struct modis_hdr *) * n))) {
		fprintf(stderr, "not enough memory\n");
		return(10);
	}
	if(!(buf_hdr = malloc(sizeof(unsigned char *) * n))) {
		fprintf(stderr, "not enough memory\n");
		return(10);
	}
	if(!(buf_data = malloc(sizeof(unsigned char *) * n))) {
		fprintf(stderr, "not enough memory\n");
		return(10);
	}

	/* allocate data memory */
	for(i = 0; i < n; i++) {
		if(!(hdr[i] = malloc(sizeof(struct pri_hdr)))) {
			fprintf(stderr, "not enough memory\n");
			return(10);
		}
		hdr[i]->flag = 0;
		if(!(mhdr[i] = malloc(sizeof(struct modis_hdr)))) {
			fprintf(stderr, "not enough memory\n");
			return(10);
		}
		if(!(buf_hdr[i] = malloc(sizeof(unsigned char) * PRI_HDR_SIZE))) {
			fprintf(stderr, "not enough memory\n");
			return(10);
		}
		if(!(buf_data[i] = malloc(sizeof(unsigned char) * DATA_SIZE))) {
			fprintf(stderr, "not enough memory\n");
			return(10);
		}
	}

	/* open input files */
	for(i = 0; i < n; i++) {
		if(!(fin[i] = fopen(argv[i + 4], "rb"))) {
			fprintf(stderr,	"can't open input file (%s)\n", argv[i + 4]);
			return(10);
		}
	}

	/* open output file */
	if(!(fout = fopen(argv[n + 4], "wb"))) {
		fprintf(stderr, "can't create output file (%s)\n", argv[n + 4]);
		return(10);
	}

	/* main loop */
	for(;;) {
		/* for each input file */
		for(i = 0; i < n; i++) {
			/* do until we have a valid packet or we have reached EOF */
			for(;hdr[i]->flag == 0;) {
				/* read primary header */
				if(ReadPriHdr(fin[i], buf_hdr[i])) {
					/* end of file? */
					if(feof(fin[i])) {
						hdr[i]->flag = -1;
						break;
					} else {
						fprintf(stderr,
										"error reading input file (%s)\n",
										argv[i + 4]);
						return(5);
					}
				}
					
				/* decode primary header */
				switch(error = DecodePriHdr(buf_hdr[i], hdr[i])) {
				case 0:
					break;
				case -1:
					fprintf(stderr,
									"unsupported packet version (%d) in input file "
									"(%s): "
									"file might be corrupted, trying to resyncronise\n",
									hdr[i]->version,
									argv[i + 4]);

					/* read data block */
					if(hdr[i]->pkt_length + 1 > DATA_SIZE) {
						fprintf(stderr,
										"buffer overflow (%d), "
										"please contact developer\n",
										hdr[i]->pkt_length);
						return(20);
					}
					if(fread(buf_data[i], hdr[i]->pkt_length + 1, 1, fin[i]) !=
						 1) {
						fprintf(stderr,
										"error reading input file (%s)\n",
										argv[i + 4]);
						return(5);
					}
					continue;
				default:
					fprintf(stderr,
									"unknown error (%d) while decoding primary "
									"header\n",
									error);
					return(5);
				}
					
				/* read data block */
				if(hdr[i]->pkt_length + 1 > DATA_SIZE) {
					fprintf(stderr,
									"buffer overflow (%d), "
									"please contact developer\n",
									hdr[i]->pkt_length);
					return(20);
				}
				if(fread(buf_data[i], hdr[i]->pkt_length + 1, 1, fin[i]) !=
					 1) {
					fprintf(stderr,
									"error reading input file (%s)\n",
									argv[i + 4]);
					return(5);
				}

				/* is it a packet we need? */
				if(hdr[i]->apid != apid)
					continue;

				/* decode MODIS header */
				DecodeMODISHdr(buf_data[i],
											 hdr[i]->pkt_length + 1,
											 mhdr[i]);

				/* calculate checksum */
				chksum =
					CalcChecksum12(&(buf_data[i][MODIS_HDR_SIZE]),
												 (hdr[i]->pkt_length + 1 - MODIS_HDR_SIZE) /
												 1.5 - 1);

				/* invalid packet? */
				if(chksum != mhdr[i]->checksum)
					continue;

				/* before startdate? */
				if((mhdr[i]->days < startday) ||
					 ((mhdr[i]->days == startday) &&
						(mhdr[i]->millisec < startmillisec)))
					continue;

				/* after enddate? */
				if((mhdr[i]->days > endday) ||
					 ((mhdr[i]->days == endday) &&
						(mhdr[i]->millisec >= endmillisec)))
					continue;

				/* set valid packet flag */
				hdr[i]->flag = 1;
			}
		}

		/* find the oldest packet */
		for(i = 0, valpkt = 0; i < n; i++) {
			/* invalid packet? */
			if(hdr[i]->flag != 1)
				continue;

			/* first valid packet? */
			if(valpkt == 0) {
				/* set oldest to this this packet */
				oldest = i;

				/* set flag for valid packet available */
				valpkt = 1;

				/* next stream */
				continue;
			}

			/* test days */
			if(mhdr[i]->days < mhdr[oldest]->days) {
				oldest = i;
				continue;
			}
			if(mhdr[i]->days > mhdr[oldest]->days) {
				continue;
			}

			/* test milliseconds */
			if(mhdr[i]->millisec < mhdr[oldest]->millisec) {
				oldest = i;
				continue;
			}
			if(mhdr[i]->millisec > mhdr[oldest]->millisec) {
				continue;
			}

			/* test microseconds */
			if(mhdr[i]->microsec < mhdr[oldest]->microsec) {
				oldest = i;
				continue;
			}
			if(mhdr[i]->microsec > mhdr[oldest]->microsec) {
				continue;
			}

			/* test packet count */
			pktdiff = hdr[i]->pkt_count - hdr[oldest]->pkt_count;
			if(pktdiff < -8191) pktdiff += 16384;
			if(pktdiff > 8191) pktdiff -= 16384;
			if(pktdiff < 0) {
				oldest = i;
				continue;
			}
			if(pktdiff > 0) {
				continue;
			}

			/* identical packets, flag packet from stream i invalid */
			hdr[i]->flag = 0;
		}

		/* no valid packet available? */
		if(valpkt == 0)
			break;

		/* avoid duplicated and old packets */
		if(mhdr[oldest]->days < lastdays) {
			hdr[oldest]->flag = 0;
			lastdays = mhdr[oldest]->days;
			lastmillisec = mhdr[oldest]->millisec;
			lastmicrosec = mhdr[oldest]->microsec;
			lastpktcount = hdr[oldest]->pkt_count;
			continue;
		}
		if(mhdr[oldest]->days == lastdays) {
			if(mhdr[oldest]->millisec < lastmillisec) {
				hdr[oldest]->flag = 0;
				lastdays = mhdr[oldest]->days;
				lastmillisec = mhdr[oldest]->millisec;
				lastmicrosec = mhdr[oldest]->microsec;
				lastpktcount = hdr[oldest]->pkt_count;
				continue;
			}
			if(mhdr[oldest]->millisec == lastmillisec) {
				if(mhdr[oldest]->microsec < lastmicrosec) {
					hdr[oldest]->flag = 0;
					lastdays = mhdr[oldest]->days;
					lastmillisec = mhdr[oldest]->millisec;
					lastmicrosec = mhdr[oldest]->microsec;
					lastpktcount = hdr[oldest]->pkt_count;
					continue;
				}
				if(mhdr[oldest]->microsec == lastmicrosec) {
					pktdiff = hdr[oldest]->pkt_count - lastpktcount;
					if(pktdiff < -8191) pktdiff += 16384;
					if(pktdiff > 8191) pktdiff -= 16384;
					if(pktdiff <= 0) {
						hdr[oldest]->flag = 0;
						lastdays = mhdr[oldest]->days;
						lastmillisec = mhdr[oldest]->millisec;
						lastmicrosec = mhdr[oldest]->microsec;
						lastpktcount = hdr[oldest]->pkt_count;
						continue;
						/*	}*/
					}
				}
			}
		}

		/* store packet date/time/sample/pktcount */
		lastdays = mhdr[oldest]->days;
		lastmillisec = mhdr[oldest]->millisec;
		lastmicrosec = mhdr[oldest]->microsec;
		lastpktcount = hdr[oldest]->pkt_count;
		
		/* write packet to output file */
		if(WritePriHdr(fout, buf_hdr[oldest])) {
			fprintf(stderr,
							"error writing to output file (%s)\n",
							argv[n + 4]);
			return(5);
		}
		if(fwrite(buf_data[oldest],
							hdr[oldest]->pkt_length + 1,
							1, fout) != 1) {
			fprintf(stderr,
							"error writing to output file (%s)\n",
							argv[n + 4]);
			return(5);
		}

		/* flag packet from stream oldest invalid */
		hdr[oldest]->flag = 0;
	}
	
	/* close output file */
	fclose(fout);

	/* close input files */
	for(i = 0; i < n; i++){
		fclose(fin[i]);
	}

	/* free memory */
	for(i = 0; i < n; i++) {
		free(buf_data[i]);
		free(buf_hdr[i]);
		free(mhdr[i]);
		free(hdr[i]);
	}
	free(buf_data);
	free(buf_hdr);
	free(mhdr);
	free(hdr);
	free(fin);

	/* Ja das war's. Der Pop-Shop ist zu Ende */
  return(0);
}


/********************************************************************
 *                                                                  *
 *  read primary header from file                                   *
 *                                                                  *
 *  f:   file pointer                                               *
 *  buf: pointer to buffer                                          *
 *                                                                  *
 *  result:  0 - ok                                                 *
 *          -1 - read error                                         *
 *                                                                  *
 ********************************************************************/
int ReadPriHdr(FILE *f, unsigned char *buf) {
	/* read header */
	if(fread(buf, PRI_HDR_SIZE, 1, f) != 1)
		return(-1);

	/* ois rodger */
	return(0);
}


/********************************************************************
 *                                                                  *
 *  write primary header to file                                    *
 *                                                                  *
 *  f:   file pointer                                               *
 *  buf: pointer to buffer                                          *
 *                                                                  *
 *  result:  0 - ok                                                 *
 *          -1 - write error                                        *
 *                                                                  *
 ********************************************************************/
int WritePriHdr(FILE *f, unsigned char *buf) {
	/* read header */
	if(fwrite(buf, PRI_HDR_SIZE, 1, f) != 1)
		return(-1);

	/* ois rodger */
	return(0);
}


/********************************************************************
 *                                                                  *
 *  decode primary header                                           *
 *                                                                  *
 *  buf: pointer to buffer                                          *
 *  hdr: pointer to header structure                                *
 *                                                                  *
 *  result:  0 - ok                                                 *
 *          -1 - decode error (version not supported)               *
 *                                                                  *
 ********************************************************************/
int DecodePriHdr(unsigned char *buf, struct pri_hdr *hdr) {
	/* version */
	hdr->version = (buf[0] & 0xE0) >> 5;

	/* version supported? */
	if(hdr->version != 0)
		return(-1);

	/* type */
	hdr->type = (buf[0] & 0x10) >> 4;

	/* secondary header flag */
	hdr->sec_hdr_flag = (buf[0] & 0x08) >> 3;

	/* APID */
	hdr->apid = (((int)(buf[0] & 0x07)) << 8) + buf[1];

	/* sequence flags */
	hdr->seq_flags = (buf[2] & 0xC0) >> 6;

	/* packet count per APID */
	hdr->pkt_count = (((int)(buf[2] & 0x3F)) << 8) + buf[3];

	/* packet length (length - 1) */
	hdr->pkt_length = (((int)buf[4]) << 8) + buf[5];

	/* okeydokey */
	return(0);
}


/********************************************************************
 *                                                                  *
 *  decode MODIS header                                             *
 *                                                                  *
 *  buf: pointer to buffer                                          *
 *  len: data lengh                                                 *
 *  hdr: pointer to header structure                                *
 *                                                                  *
 *  result:  0 - ok                                                 *
 *          -1 - decode error                                       *
 *          -2 - decode error                                       *
 *                                                                  *
 ********************************************************************/
int DecodeMODISHdr(unsigned char *buf, int len,
									 struct modis_hdr *hdr) {
	/* days since 01/01/1958 */
	hdr->days =
		(((int)buf[0]) << 8) +
		(((int)buf[1]));
	
	/* milliseconds of day */
	hdr->millisec =
		(((unsigned long int)buf[2]) << 24) +
		(((unsigned long int)buf[3]) << 16) +
		(((unsigned long int)buf[4]) << 8) +
		(((unsigned long int)buf[5]));
	
	/* microseconds of milliseconds */
	hdr->microsec =
		(((int)buf[6]) << 8) +
		(((int)buf[7]));
	
	/* quicklook flag */
	hdr->ql = (buf[8] & 0x80) >> 7;
	
	/* packet type */
	hdr->pkt_type = (buf[8] & 0x70) >> 4;
	
	/* scan count */
	hdr->scan_count = (buf[8] & 0x0E) >> 1;
	
	/* mirror side */
	hdr->mirror_side = (buf[8] & 1);
	
	/* source identification (0 = earth, 1 = calibration) */
	hdr->src1 = (buf[9] & 0x80) >> 7;

	/* source identification (0 = eng., 1 to 1354 = sample count) */
	hdr->src2 =
		(((int)buf[9] & 0x7F) << 4) +
		(((int)buf[10] & 0xF0) >> 4);

	/* FPA/AEM config */
	hdr->conf =
		(((int)buf[10] & 0x0F) << 6) +
		(((int)buf[11] & 0xFC) >> 2);

	/* sci state */
	hdr->sci_state = (((int)buf[11] & 0x02) >> 1);

	/* sci abnorm */
	hdr->sci_abnorm = (((int)buf[11] & 0x01));

	/* check sum */
	hdr->checksum =
		(((int)buf[len - 2] & 0x0F) << 8) +
		(((int)buf[len - 1]));
}


/********************************************************************
 *                                                                  *
 *  convert calendar date to julian day                             *
 *                                                                  *
 *  minute: minute of time to convert                               *
 *  hour:   hour of time to convert                                 *
 *  day:    day of date to convert                                  *
 *  month:  month of date to convert                                *
 *  year:   year of date to convert                                 *
 *  jul:    pointer to store julian day                             *
 *                                                                  *
 *  result: none                                                    *
 *                                                                  *
 ********************************************************************/
void julday(int minute, int hour, int day, int month, int year,
						double *jul) {
	/* julian day as long */
	long ljul;
	/* helping variables */
	int ja, jy, jm;


	jy = year;

	if(jy < 0)
		++jy;
	if(month > 2) {
		jm = month + 1;
	} else {
		--jy;
		jm = month + 13;
	}
	ljul = (long)(floor(365.25 * jy) + floor(30.6001 * jm) + day +
								1720995);
	if(day + 31L * (month + 12L * year) >= (15+31L*(10+12L*1582))) {
		ja = (int)(0.01 * jy);
		ljul += 2 - ja + (int)(0.25 * ja);
	}

	*jul =
		(double)ljul +
		(double)hour / 24.0 +
		(double)minute / 1440.0 +
		0.000001; /* add about 0.1s to avoid precision problems */
}


/********************************************************************
 *                                                                  *
 *  convert julian day to calendar date                             *
 *                                                                  *
 *  minute: pointer to store minute                                 *
 *  hour:   pointer to store hour                                   *
 *  day:    pointer to store day                                    *
 *  month:  pointer to store month                                  *
 *  year:   pointer to store year                                   *
 *  jul:    julian day                                              *
 *                                                                  *
 *  result: none                                                    *
 *                                                                  *
 ********************************************************************/
void caldat(int *minute, int *hour, int *day, int *month, int *year,
						double jul) {
	/* julian day as long */
	long ljul;
	/* helping variables */
	long ja, jalpha, jb, jc, jd, je;


	ljul = (long)floor(jul);
	jul -= (double)ljul;
	*hour = (int)(floor(jul * 24.0));
	jul -= (double)*hour / 24.0;
	*minute = (int)(floor(jul * 1440.0));

	if(ljul >= 2299161) {
		jalpha = (long)(((float)(ljul - 1867216) - 0.25) / 36524.25);
		ja = ljul + 1 + jalpha -(long)(0.25 * jalpha);
	} else
		ja = ljul;
	jb = ja + 1524;
	jc = (long)(6680.0 + ((float)(jb - 2439870) - 122.1) / 365.25);
	jd = (long)(365 * jc + (0.25 * jc));
	je = (long)((jb - jd) / 30.6001);
	*day = jb - jd -(long)(30.6001*je);
	*month = je - 1;
	if(*month > 12)
		*month -= 12;
	*year = jc - 4715;
	if(*month > 2)
		--(*year);
	if(*year <= 0)
		--(*year);
}


/********************************************************************
 *                                                                  *
 *  calculate 12bit checksum                                        *
 *                                                                  *
 *  buf: pointer to data buffer                                     *
 *  n:   number of bytes in buffer                                  *
 *                                                                  *
 *  result: checksum                                                *
 *                                                                  *
 ********************************************************************/
int CalcChecksum12(unsigned char *buf, int n) {
	/* counter */
	int i;
	/* data value */
	unsigned long x;
	/* checksum */
	unsigned long s = 0;

	
	/* main loop */
	for(i = 0; i < n; i++) {
		/* get 1. value */
		x =
			(((unsigned long)buf[(int)(1.5 * i)]) << 4) +
			(((unsigned long)buf[(int)(1.5 * i) + 1] & 0xF0) >> 4);

		/* add to checksum */
		s = s + x;

		/* increase counter */
		i++;

		/* do we have a second value */
		if(i >= n)
			break;

		/* get 2. value */
		x =
			(((unsigned long)buf[(int)(1.5 * (i - 1)) + 1] & 0x0F) << 8) +
			(((unsigned long)buf[(int)(1.5 * (i - 1)) + 2]));

		/* add to checksum */
		s = s + x;
	}

	
	s = s >> 4;
	s = s & 0xFFF;

	/* return checksum */
	return(s);
}
