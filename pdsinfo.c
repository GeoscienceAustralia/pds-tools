/********************************************************************
 *                                                                  *
 *  Copyright (C) 2007, 2008, 2009                                  *
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
 *  provide info about contents of a PDS file                       *
 *                                                                  *
 *  05/12/2007  S W Maier    start of work                          *
 *  12/12/2007  S W Maier    initial version                        *
 *  14/12/2007  S W Maier    added count of missing packets         *
 *  03/06/2008  S W Maier    added count of missing seconds         *
 *  14/08/2008  S W Maier    fix bug in missing seconds calcula-    *
 *                           tion when covering two days            *
 *  10/10/2008  S W Maier    increased data buffer and test for     *
 *                           packet size before reading             *
 *  09/12/2008  S W Maier    test if we have found any valid pkts   *
 *  14/02/2009  S W Maier    output number of day, night and eng.   *
 *                           pkts                                   *
 *  24/03/2009  S W Maier    proper handling of corrupted files     *
 *                                                                  *
 ********************************************************************
 *                                                                  *
 *  usage: pdsinfo <input>                                          *
 *                                                                  *
 ********************************************************************
 *                                                                  *
 * to do:                                                           *
 *  - support other sensors                                         *
 *                                                                  *
 ********************************************************************
 *                                                                  *
 *  build: cc pdsinfo.c -lm -o pdsinfo                              *
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
#define NAME "pdsinfo"
/* version */
#define VERSION 1
/* revision */
#define REVISION 6
/* primary header size */
#define PRI_HDR_SIZE 6
/* MODIS secondary header size */
#define MODIS_HDR_SIZE 12
/* Julian Day of MODIS reference date (01/01/1958)*/
#define MODIS_REF_DATE 2436205.0
/* data buffer size */
#define DATA_SIZE 100000


/********************************************************************
 *                                                                  *
 *  structure definitions                                           *
 *                                                                  *
 ********************************************************************/
/* primary header */
struct pri_hdr {
	int version;
	int type;
	int sec_hdr_flag;
	int apid;
	int seq_flags;
	int pkt_count;
	int pkt_length;
};

/* APID info */
struct apid_info {
	int apid;
	long int count;
	long int invalid;
	long int missing;
	long int last_pkt_count;
	struct apid_info *next;
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
int DecodePriHdr(unsigned char *buf, struct pri_hdr *hdr);
int DecodeMODISHdr(unsigned char *buf, int len,
									 struct modis_hdr *hdr);
struct apid_info *AllocAPIDInfo(int apid);
void AddAPIDInfo(struct apid_info **list, struct apid_info *ai);
void FreeAPIDInfoList(struct apid_info *list);
struct apid_info *FindAPIDInfo(struct apid_info *list, int apid);
void caldat(int *minute, int *hour, int *day, int *month, int *year,
						double jul);
int CalcChecksum12(unsigned char *buf, int n);


/********************************************************************
 *                                                                  *
 *  main function                                                   *
 *                                                                  *
 ********************************************************************/
int main(int argc, char *argv[])
{
	/* file pointer */
  FILE *fin;
	/* primary header structure */
	struct pri_hdr hdr;
	/* MODIS header structure */
	struct modis_hdr mhdr;
	/* header buffer */
	unsigned char buf_hdr[PRI_HDR_SIZE];
	/* data buffer */
	unsigned char *buf_data;
	/* pointer to APID Info list */
	struct apid_info *apidlist = NULL;
	/* pointer to current APID Info object */
	struct apid_info *apidinfo = NULL;
	/* first packet date/time */
	long int firstday = 1.E6, firstms = 1.E6, firstmics = 1.E6;
	/* last packet date/time */
	long int lastday = 0, lastms = 0, lastmics = 0;
	/* previous packet date/time */
	long int prevday = 0, prevms = 0, prevmics = 0;
	/* date buffer */
	int second, minute, hour, day, month, year;
	long int ms;
	double jul;
	/* checksum */
	int chksum;
	/* error code */
	int error;
	/* number of missing packets */
	long int missing;
	/* number of missing seconds */
	long int missingsecs = 0;
	/* millisecs difference between packets */
	long int diffms;
	/* */
	int lastdays, lastmicrosec, lastsrc;
	unsigned long int lastmillisec;
	/* number of day packets */
	long int daypkts1 = 0, daypkts2 = 0;
	/* number of night packets */
	long int nightpkts1 = 0, nightpkts2 = 0;
	/* number of engineering packets */
	long int engpkts1 = 0, engpkts2 = 0;
	/* return value */
	int retvalue = 0;


	/* print version information */
	fprintf(stderr, "%s V%d.%d ("__DATE__")\n",
					NAME,	VERSION, REVISION);

	/* check number of arguments */
	if(argc != 2) {
		fprintf(stderr, "USAGE: %s <input>\n", argv[0]);
		return(20);
	}

	/* allocate memory */
	if(!(buf_data = malloc(sizeof(unsigned char) * DATA_SIZE))) {
		fprintf(stderr, "not enough memory\n");
		return(10);
	}

	/* open input file */
	if(!(fin = fopen(argv[1], "rb"))) {
		fprintf(stderr, "can't open input file (%s)\n", argv[1]);
		return(10);
	}

	/* main loop */
	for(;;) {
		/* read primary header */
		if(ReadPriHdr(fin, buf_hdr)) {
			/* end of file? */
			if(feof(fin))
				break;
			else {
				fprintf(stderr,
								"error 1 reading input file (%s): "
								"file might be corrupted\n",
								argv[1]);
				retvalue = 5;
				break;
			}
		}

		/* decode primary header */
		switch(error = DecodePriHdr(buf_hdr, &hdr)) {
		case 0:
			break;
		case -1:
			fprintf(stderr,
							"unsupported packet version (%d): "
							"file might be corrupted, trying to resyncronise\n",
							hdr.version);

			/* read data block */
			if(fread(buf_data, hdr.pkt_length + 1, 1, fin) != 1) {
				fprintf(stderr,
								"error 2 reading input file (%s): "
								"file might be corrupted\n",
								argv[1]);
				retvalue = 5;
				break;
			}
			continue;
		default:
			fprintf(stderr,
							"unknown error (%d) while decoding primary header\n",
							error);
			return(5);
		}

		/* do we have a current APID Info object? */
		if(apidinfo != NULL) {
			/* APID different as of current APID Info object? */
			if(apidinfo->apid != hdr.apid) {
				/* find APID Info object */
				if(!(apidinfo = FindAPIDInfo(apidlist, hdr.apid))) {
					/* allocate an APID Info object */
					if(!(apidinfo = AllocAPIDInfo(hdr.apid))) {
						fprintf(stderr, "can't allocate memory\n");
						return(5);
					}

					/* add APID Info object to list */
					AddAPIDInfo(&apidlist, apidinfo);
				}
			}
		} else {
			/* allocate an APID Info object */
			if(!(apidinfo = AllocAPIDInfo(hdr.apid))) {
				fprintf(stderr, "can't allocate memory\n");
				return(5);
			}
			
			/* add APID Info object to list */
			AddAPIDInfo(&apidlist, apidinfo);
		}

		/* increase packet counter */
		apidinfo->count = apidinfo->count + 1;

		/* check if there are missing packets */
		/* first packet with this APID? */
		if(apidinfo->last_pkt_count != -1) {
			/* calculate number of missing packets */
			missing =
				(hdr.pkt_count > apidinfo->last_pkt_count)?
				(hdr.pkt_count - apidinfo->last_pkt_count - 1):
				(hdr.pkt_count - apidinfo->last_pkt_count + 16383);

			/* duplicated packet? */
			if(missing == 16383)
				fprintf(stderr, "duplicated packet!!!\n");

			/* add to counter for missing packets */
			apidinfo->missing += missing;
		}

		/* store packet count */
		apidinfo->last_pkt_count = hdr.pkt_count;

		/* read data block */
		if(hdr.pkt_length + 1 > DATA_SIZE) {
			fprintf(stderr,
							"buffer overflow (%d), "
							"please contact developer\n",
							hdr.pkt_length);
			return(20);
		}
		if(fread(buf_data, hdr.pkt_length + 1, 1, fin) != 1) {
			fprintf(stderr,
							"error 3 reading input file (%s): "
							"file might be corrupted\n",
							argv[1]);
			retvalue = 5;
			break;
		}

		/* is it a MODIS packet? */
		if((hdr.apid >= 64) && (hdr.apid <=127)) {
			/* decode MODIS header */
			DecodeMODISHdr(buf_data, hdr.pkt_length + 1, &mhdr);

			/* duplicated packet? */
			if(missing == 16383) {
				fprintf(stderr, "duplicated MODIS packet: %d/%d %ld/%ld %d/%d %d/%d\n",
								mhdr.days, lastdays,
								mhdr.millisec, lastmillisec,
								mhdr.microsec, lastmicrosec,
								mhdr.src2, lastsrc);
			}
			lastdays = mhdr.days;
			lastmillisec = mhdr.millisec;
			lastmicrosec = mhdr.microsec;
			lastsrc = mhdr.src2;

			/* calculate checksum */
			chksum = CalcChecksum12(&(buf_data[MODIS_HDR_SIZE]),
															(hdr.pkt_length + 1 - MODIS_HDR_SIZE) /
															1.5 - 1);

			/* valid packet? */
			if(chksum != mhdr.checksum) {
				/* increase invalid packet counter */
				apidinfo->invalid = apidinfo->invalid + 1;
			}

			/* determine first and last packet date/time */
			if(mhdr.days < firstday) {
				firstday = mhdr.days;
				firstms = mhdr.millisec;
				firstmics = mhdr.microsec;
			} else {
				if(mhdr.days == firstday) {
					if(mhdr.millisec < firstms) {
						firstday = mhdr.days;
						firstms = mhdr.millisec;
						firstmics = mhdr.microsec;
					} else {
						if(mhdr.millisec == firstms) {
							if(mhdr.microsec < firstmics) {
								firstday = mhdr.days;
								firstms = mhdr.millisec;
								firstmics = mhdr.microsec;
							}
						}
					}
				}
			}
			if(mhdr.days > lastday) {
				lastday = mhdr.days;
				lastms = mhdr.millisec;
				lastmics = mhdr.microsec;
			} else {
				if(mhdr.days == lastday) {
					if(mhdr.millisec > lastms) {
						lastday = mhdr.days;
						lastms = mhdr.millisec;
						lastmics = mhdr.microsec;
					} else {
						if(mhdr.millisec == lastms) {
							if(mhdr.microsec > lastmics) {
								lastday = mhdr.days;
								lastms = mhdr.millisec;
								lastmics = mhdr.microsec;
							}
						}
					}
				}
			}

			/* check if there are missing seconds */
			if(prevday != 0) {
				diffms =
					mhdr.millisec - prevms +
					(mhdr.days - prevday) * 86400000;
				missingsecs += diffms / 1000;
			}
			prevday = mhdr.days;
			prevms = mhdr.millisec;
			prevmics = mhdr.microsec;

			/* earth view packet? */
			if(mhdr.src1 == 0) {
				/* increase packet type counters? */
				switch(mhdr.pkt_type) {
				case 0:
					daypkts1++;
					break;
				case 1:
					nightpkts1++;
					break;
				case 2:
				case 4:
					engpkts1++;
					break;
				}
			} else {
				/* increase packet type counters? */
				switch(mhdr.pkt_type) {
				case 0:
					daypkts2++;
					break;
				case 1:
					nightpkts2++;
					break;
				case 2:
				case 4:
					engpkts2++;
					break;
				}
			}
		}
	}

	/* have we read any valid packets? */
	if(apidinfo == NULL) {
		fprintf(stderr, "no valid packets found\n");
		return(5);
	}	

	/* print APID statistics */
	for(apidinfo = apidlist;
			apidinfo != NULL;
			apidinfo = apidinfo->next) {
		printf("APID %d: count %ld invalid %ld missing %ld\n",
					 apidinfo->apid,
					 apidinfo->count,
					 apidinfo->invalid,
					 apidinfo->missing);
	}

	/* print first and last packet date/time */
	jul = firstday + MODIS_REF_DATE;
	caldat(&minute, &hour, &day, &month, &year, jul);
	hour = firstms / (1000L * 60L * 60L);
	ms = firstms - hour * 1000L * 60L * 60L;
	minute = ms / (1000L * 60L);
	ms = ms - minute * 1000L * 60L;
	second = ms / 1000L;
	ms = ms - second * 1000L;
	printf("first packet: %04d/%02d/%02d %02d:%02d:%d.%03d%03d\n",
				 year, month, day, hour, minute, second, ms, firstmics);
	jul = lastday + MODIS_REF_DATE;
	caldat(&minute, &hour, &day, &month, &year, jul);
	hour = lastms / (1000L * 60L * 60L);
	ms = lastms - hour * 1000L * 60L * 60L;
	minute = ms / (1000L * 60L);
	ms = ms - minute * 1000L * 60L;
	second = ms / 1000L;
	ms = ms - second * 1000L;
	printf("last packet: %04d/%02d/%02d %02d:%02d:%d.%03d%03d\n",
				 year, month, day, hour, minute, second, ms, lastmics);

	/* print number of missing secs */
	printf("missing seconds: %ld\n", missingsecs);

	/* print number of day packets */
	printf("day packets: %ld/%ld\n", daypkts1, daypkts2);

	/* print number of night packets */
	printf("night packets: %ld/%ld\n", nightpkts1, nightpkts2);

	/* print number of engineering packets */
	printf("engineering packets: %ld/%ld\n", engpkts1, engpkts2);

	/* free APID Info list */
	FreeAPIDInfoList(apidlist);

	/* close input file */
	fclose(fin);

	/* free memory */
	free(buf_data);

	/* Ja das war's. Der Pop-Shop ist zu Ende */
  return(retvalue);
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
 *  allocate and initialise APID Info object                        *
 *                                                                  *
 *  apid: APID                                                      *
 *                                                                  *
 *  result:  pointer to APID Info object                            *
 *           0 - error                                              *
 *                                                                  *
 ********************************************************************/
struct apid_info *AllocAPIDInfo(int apid) {
	/* pointer to APID Info object */
	struct apid_info *ai;


	/* allocate memory for object */
	if(!(ai = malloc(sizeof(struct apid_info))))
		return(0);

	/* store APID */
	ai->apid = apid;

	/* initialise packet counter */
	ai->count = 0;

	/* initialise invalid packet counter */
	ai->invalid = 0;

	/* initialise missing packet counter */
	ai->missing = 0;

	/* initialise last packet count value */
	ai->last_pkt_count = -1;

	/* initialise pointer to next object */
	ai->next = NULL;

	/* all done */
	return(ai);
}


/********************************************************************
 *                                                                  *
 *  add APID Info object to list                                    *
 *                                                                  *
 *  list: pointer to pointer to first object in list                *
 *  ai:   pointer to APID Info object to be added                   *
 *                                                                  *
 *  result:  none                                                   *
 *                                                                  *
 ********************************************************************/
void AddAPIDInfo(struct apid_info **list, struct apid_info *ai) {
	/* pointer to APID Info object */
	struct apid_info *p;


	/* empty list? */
	if(*list == NULL) {
		/* add object as first in list */
		*list = ai;

		/* ciao */
		return;
	}

	/* first object? */
	if((*list)->apid > ai->apid) {
		/* add object as first in list */
		ai->next = *list;
		*list = ai;

		/* Tschuess */
		return;
	}

	/* find right place in list */
	for(p = *list; p->next != NULL; p = p->next) {
		/* reached right position */
		if(p->next->apid > ai->apid) {
			/* add object to list */
			ai->next = p->next;
			p->next = ai;

			/* all good */
			return;
		}
	}

	/* add object as last in list */
	p->next = ai;
	
	/* hasta la vista*/
	return;
}


/********************************************************************
 *                                                                  *
 *  free memory in APID list                                        *
 *                                                                  *
 *  list: pointer to first object in list                           *
 *                                                                  *
 *  result:  none                                                   *
 *                                                                  *
 ********************************************************************/
void FreeAPIDInfoList(struct apid_info *list) {
	/* pointers to APID Info objects */
	struct apid_info *p1, *p2;


	/* go through list */
	for(p1 = list; p1 != NULL; p1 = p2) {
		/* get pointer to next object */
		p2 = p1->next;

		/* free memory of current object */
		free(p1);
	}
}


/********************************************************************
 *                                                                  *
 *  find APID Info object                                           *
 *                                                                  *
 *  list: pointer to first object in list                           *
 *  apid: APID                                                      *
 *                                                                  *
 *  result:  pointer to APID Info object                            *
 *           0 - APID Info object not found                         *
 *                                                                  *
 ********************************************************************/
struct apid_info *FindAPIDInfo(struct apid_info *list, int apid) {
	/* pointer to APID Info object */
	struct apid_info *p;


	/* go through list */
	for(p = list; p != NULL; p = p->next) {
		/* right APID? */
		if(p->apid == apid)
			/* return object address */
			return(p);
	}

	/* couldn't find object */
	return(NULL);
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
	
	/* packet type (000 = day, 001 = night, 010 = eng1, 100 = eng2 */
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
