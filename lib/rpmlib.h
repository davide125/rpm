#ifndef H_RPMLIB
#define H_RPMLIB

/* This is the *only* module users of rpmlib should need to include */

#include <db.h>

/* it shouldn't need these :-( */
#include "dbindex.h"
#include "header.h"

struct rpmTagTableEntry {
    char * name;
    int val;
};

int rpmReadPackageInfo(int fd, Header * signatures, Header * hdr);
int rpmReadPackageHeader(int fd, Header * hdr, int * isSource, int * major,
			 int * minor);
   /* 0 = success */
   /* 1 = bad magic */
   /* 2 = error */

extern const struct rpmTagTableEntry rpmTagTable[];
extern const int rpmTagTableSize;

/* these tags are for both the database and packages */
/* none of these can be 0 !!                         */

#define RPMTAG_NAME  			1000
#define RPMTAG_VERSION			1001
#define RPMTAG_RELEASE			1002
#define RPMTAG_SERIAL   		1003
#define	RPMTAG_SUMMARY			1004
#define RPMTAG_DESCRIPTION		1005
#define RPMTAG_BUILDTIME		1006
#define RPMTAG_BUILDHOST		1007
#define RPMTAG_INSTALLTIME		1008
#define RPMTAG_SIZE			1009
#define RPMTAG_DISTRIBUTION		1010
#define RPMTAG_VENDOR			1011
#define RPMTAG_GIF			1012
#define RPMTAG_XPM			1013
#define RPMTAG_COPYRIGHT                1014
#define RPMTAG_PACKAGER                 1015
#define RPMTAG_GROUP                    1016
#define RPMTAG_CHANGELOG                1017
#define RPMTAG_SOURCE                   1018
#define RPMTAG_PATCH                    1019
#define RPMTAG_URL                      1020
#define RPMTAG_OS                       1021
#define RPMTAG_ARCH                     1022
#define RPMTAG_PREIN                    1023
#define RPMTAG_POSTIN                   1024
#define RPMTAG_PREUN                    1025
#define RPMTAG_POSTUN                   1026
#define RPMTAG_FILENAMES		1027
#define RPMTAG_FILESIZES		1028
#define RPMTAG_FILESTATES		1029
#define RPMTAG_FILEMODES		1030
#define RPMTAG_FILEUIDS			1031
#define RPMTAG_FILEGIDS			1032
#define RPMTAG_FILERDEVS		1033
#define RPMTAG_FILEMTIMES		1034
#define RPMTAG_FILEMD5S			1035
#define RPMTAG_FILELINKTOS		1036
#define RPMTAG_FILEFLAGS		1037
#define RPMTAG_ROOT                     1038
#define RPMTAG_FILEUSERNAME             1039
#define RPMTAG_FILEGROUPNAME            1040
#define RPMTAG_EXCLUDE                  1041 /* not used */
#define RPMTAG_EXCLUSIVE                1042 /* not used */
#define RPMTAG_ICON                     1043
#define RPMTAG_SOURCERPM                1044
#define RPMTAG_FILEVERIFYFLAGS          1045
#define RPMTAG_ARCHIVESIZE              1046
#define RPMTAG_PROVIDES                 1047
#define RPMTAG_REQUIREFLAGS             1048
#define RPMTAG_REQUIRENAME              1049
#define RPMTAG_REQUIREVERSION           1050
#define RPMTAG_NOSOURCE                 1051
#define RPMTAG_NOPATCH                  1052
#define RPMTAG_CONFLICTFLAGS            1053
#define RPMTAG_CONFLICTNAME             1054
#define RPMTAG_CONFLICTVERSION          1055
#define RPMTAG_DEFAULTPREFIX            1056
#define RPMTAG_BUILDROOT                1057
#define RPMTAG_INSTALLPREFIX		1058
#define RPMTAG_EXCLUDEARCH              1059
#define RPMTAG_EXCLUDEOS                1060
#define RPMTAG_EXCLUSIVEARCH            1061
#define RPMTAG_EXCLUSIVEOS              1062
#define RPMTAG_AUTOREQPROV              1063 /* used internally by builds */
#define RPMTAG_RPMVERSION		1064
#define RPMTAG_TRIGGERSCRIPTS           1065
#define RPMTAG_TRIGGERNAME              1066
#define RPMTAG_TRIGGERVERSION           1067
#define RPMTAG_TRIGGERFLAGS             1068
#define RPMTAG_TRIGGERINDEX             1069
#define RPMTAG_VERIFYSCRIPT             1079

#define RPMTAG_EXTERNAL_TAG		1000000

#define RPMFILE_STATE_NORMAL 		0
#define RPMFILE_STATE_REPLACED 		1
#define RPMFILE_STATE_NOTINSTALLED	2
#define RPMFILE_STATE_NETSHARED		3

/* these can be ORed together */
#define RPMFILE_CONFIG			(1 << 0)
#define RPMFILE_DOC			(1 << 1)
#define RPMFILE_SPECFILE                (1 << 2)

#define RPMINSTALL_REPLACEPKG           (1 << 0)
#define RPMINSTALL_REPLACEFILES         (1 << 1)
#define RPMINSTALL_TEST                 (1 << 2)
#define RPMINSTALL_UPGRADE              (1 << 3)
#define RPMINSTALL_UPGRADETOOLD         (1 << 4)
#define RPMINSTALL_NODOCS               (1 << 5)
#define RPMINSTALL_NOSCRIPTS            (1 << 6)
#define RPMINSTALL_NOARCH               (1 << 7)
#define RPMINSTALL_NOOS                 (1 << 8)

#define RPMUNINSTALL_TEST               (1 << 0)
#define RPMUNINSTALL_NOSCRIPTS          (1 << 1)

#define RPMVERIFY_NONE             0
#define RPMVERIFY_MD5              (1 << 0)
#define RPMVERIFY_FILESIZE         (1 << 1)
#define RPMVERIFY_LINKTO           (1 << 2)
#define RPMVERIFY_USER             (1 << 3)
#define RPMVERIFY_GROUP            (1 << 4)
#define RPMVERIFY_MTIME            (1 << 5)
#define RPMVERIFY_MODE             (1 << 6)
#define RPMVERIFY_RDEV             (1 << 7)
#define RPMVERIFY_ALL              ~(RPMVERIFY_NONE)

#define RPMSENSE_ANY             0
#define RPMSENSE_SERIAL          (1 << 0)
#define RPMSENSE_LESS            (1 << 1)
#define RPMSENSE_GREATER         (1 << 2)
#define RPMSENSE_EQUAL           (1 << 3)
#define RPMSENSE_PROVIDES        (1 << 4) /* only used internally by builds */
#define RPMSENSE_CONFLICTS       (1 << 5) /* only used internally by builds */
#define RPMSENSE_SENSEMASK       15       /* Mask to get senses */

#define RPMSENSE_TRIGGER_ON              (1 << 16)
#define RPMSENSE_TRIGGER_OFF             (1 << 17)

/* Stuff for maintaining "variables" like SOURCEDIR, BUILDDIR, etc */

#define RPMVAR_SOURCEDIR     		0
#define RPMVAR_BUILDDIR      		1
/* #define RPMVAR_DOCDIR        	2 -- No longer used */
#define RPMVAR_OPTFLAGS      		3
#define RPMVAR_TOPDIR        		4
#define RPMVAR_SPECDIR       		5
#define RPMVAR_ROOT          		6
#define RPMVAR_RPMDIR        		7
#define RPMVAR_SRPMDIR       		8
/* #define RPMVAR_ARCHSENSITIVE 	9  -- No longer used */
#define RPMVAR_REQUIREDISTRIBUTION	10
/* #define RPMVAR_REQUIREGROUP		11 -- No longer used */
#define RPMVAR_REQUIREVENDOR		12
#define RPMVAR_DISTRIBUTION		13
#define RPMVAR_VENDOR			14
#define RPMVAR_MESSAGELEVEL		15
#define RPMVAR_REQUIREICON		16
#define RPMVAR_TIMECHECK		17
#define RPMVAR_SIGTYPE                  18
#define RPMVAR_PGP_PATH                 19
#define RPMVAR_PGP_NAME                 20
/* #define RPMVAR_PGP_SECRING           21 -- No longer used */
/* #define RPMVAR_PGP_PUBRING           22 -- No longer used */
#define RPMVAR_EXCLUDEDOCS              23
/* #define RPMVAR_BUILDARCH             24 -- No longer used */
/* #define RPMVAR_BUILDOS               25 -- No longer used */
#define RPMVAR_BUILDROOT                26
#define RPMVAR_DBPATH                   27
#define RPMVAR_PACKAGER                 28
#define RPMVAR_FTPPROXY                 29
#define RPMVAR_TMPPATH                  30
#define RPMVAR_CPIOBIN                  31
#define RPMVAR_FTPPORT			32
#define RPMVAR_NETSHAREDPATH		33
#define RPMVAR_DEFAULTDOCDIR		34
#define RPMVAR_LASTVAR	                35 /* IMPORTANT to keep right! */

char *rpmGetVar(int var);
int rpmGetBooleanVar(int var);
void rpmSetVar(int var, char *val);

/** rpmrc.c **/

int rpmReadConfigFiles(char * file, char * arch, char * os, int building);
int rpmGetOsNum(void);
int rpmGetArchNum(void);
char *rpmGetOsName(void);
char *rpmGetArchName(void);
int rpmShowRC(FILE *f);
int rpmArchScore(char * arch);
int rpmOsScore(char * arch);

/** **/

typedef struct rpmdb * rpmdb;

typedef void (*rpmNotifyFunction)(const unsigned long amount,
			       const unsigned long total);

int rpmdbOpen (char * prefix, rpmdb * dbp, int mode, int perms);
    /* 0 on error */
int rpmdbInit(char * prefix, int perms);
    /* nonzero on error */
void rpmdbClose (rpmdb db);

unsigned int rpmdbFirstRecNum(rpmdb db);
unsigned int rpmdbNextRecNum(rpmdb db, unsigned int lastOffset);  
    /* 0 at end */

Header rpmdbGetRecord(rpmdb db, unsigned int offset);
int rpmdbFindByFile(rpmdb db, char * filespec, dbiIndexSet * matches);
int rpmdbFindByGroup(rpmdb db, char * group, dbiIndexSet * matches);
int rpmdbFindPackage(rpmdb db, char * name, dbiIndexSet * matches);
int rpmdbFindByProvides(rpmdb db, char * filespec, dbiIndexSet * matches);
int rpmdbFindByRequiredBy(rpmdb db, char * filespec, dbiIndexSet * matches);
int rpmdbFindByConflicts(rpmdb db, char * filespec, dbiIndexSet * matches);

int rpmInstallSourcePackage(char * prefix, int fd, char ** specFile,
			    rpmNotifyFunction notify, char * labelFormat);
int rpmInstallPackage(char * rootdir, rpmdb db, int fd, char * prefix, 
		      int flags, rpmNotifyFunction notify, char * labelFormat,
		      char * netsharedPath);
int rpmVersionCompare(Header first, Header second);
int rpmRemovePackage(char * prefix, rpmdb db, unsigned int offset, int test);
int rpmVerifyFile(char * prefix, Header h, int filenum, int * result);
int rpmVerifyScript(char * root, Header h, int err);
int rpmdbRebuild(char * prefix);

typedef struct rpmDependencyCheck * rpmDependencies;

struct rpmDependencyConflict {
    char * byName, * byVersion, * byRelease;
    /* these needs fields are misnamed -- they are used for the package
       which isn't needed as well */
    char * needsName, * needsVersion;
    int needsFlags;
    void * suggestedPackage;			/* NULL if none */
    enum { RPMDEP_SENSE_REQUIRES, RPMDEP_SENSE_CONFLICTS } sense;
} ;

rpmDependencies rpmdepDependencies(rpmdb db); 	       /* db may be NULL */
void rpmdepAddPackage(rpmDependencies rpmdep, Header h);
void rpmdepAvailablePackage(rpmDependencies rpmdep, Header h, void * key);
void rpmdepUpgradePackage(rpmDependencies rpmdep, Header h);
void rpmdepRemovePackage(rpmDependencies rpmdep, int dboffset);
int rpmdepCheck(rpmDependencies rpmdep, 
		struct rpmDependencyConflict ** conflicts, int * numConflicts);
void rpmdepDone(rpmDependencies rpmdep);
void rpmdepFreeConflicts(struct rpmDependencyConflict * conflicts, int
			 numConflicts);

/** messages.c **/

#define RPMMESS_DEBUG      1
#define RPMMESS_VERBOSE    2
#define RPMMESS_NORMAL     3
#define RPMMESS_WARNING    4
#define RPMMESS_ERROR      5
#define RPMMESS_FATALERROR 6

#define RPMMESS_QUIET (RPMMESS_NORMAL + 1)

void rpmIncreaseVerbosity(void);
void rpmSetVerbosity(int level);
int rpmGetVerbosity(void);
int rpmIsVerbose(void);
int rpmIsDebug(void);

/** rpmlead.c **/

#define RPMLEAD_BINARY 0
#define RPMLEAD_SOURCE 1

#define RPMLEAD_MAGIC0 0xed
#define RPMLEAD_MAGIC1 0xab
#define RPMLEAD_MAGIC2 0xee
#define RPMLEAD_MAGIC3 0xdb

/* The lead needs to be 8 byte aligned */

#define RPMLEAD_SIZE 96

struct rpmlead {
    unsigned char magic[4];
    unsigned char major, minor;
    short type;
    short archnum;
    char name[66];
    short osnum;
    short signature_type;
    char reserved[16];      /* pads to 96 bytes -- 8 byte aligned! */
} ;

struct oldrpmlead {		/* for version 1 packages */
    unsigned char magic[4];
    unsigned char major, minor;
    short type;
    short archnum;
    char name[66];
    unsigned int specOffset;
    unsigned int specLength;
    unsigned int archiveOffset;
} ;

/** rpmerr.c **/

typedef void (*rpmErrorCallBackType)(void);

void rpmError(int code, char * format, ...);
int rpmErrorCode(void);
char *rpmErrorString(void);
rpmErrorCallBackType rpmErrorSetCallback(rpmErrorCallBackType);

#define RPMERR_GDBMOPEN		-2      /* gdbm open failed */
#define RPMERR_GDBMREAD		-3	/* gdbm read failed */
#define RPMERR_GDBMWRITE	-4	/* gdbm write failed */
#define RPMERR_INTERNAL		-5	/* internal RPM error */
#define RPMERR_DBCORRUPT	-6	/* rpm database is corrupt */
#define RPMERR_OLDDBCORRUPT	-7	/* old style rpm database is corrupt */
#define RPMERR_OLDDBMISSING	-8	/* old style rpm database is missing */
#define RPMERR_NOCREATEDB	-9	/* cannot create new database */
#define RPMERR_DBOPEN		-10     /* database open failed */
#define RPMERR_DBGETINDEX	-11     /* database get from index failed */
#define RPMERR_DBPUTINDEX	-12     /* database get from index failed */
#define RPMERR_NEWPACKAGE	-13     /* package is too new to handle */
#define RPMERR_BADMAGIC		-14	/* bad magic for an RPM */
#define RPMERR_RENAME		-15	/* rename(2) failed */
#define RPMERR_UNLINK		-16	/* unlink(2) failed */
#define RPMERR_RMDIR		-17	/* rmdir(2) failed */
#define RPMERR_PKGINSTALLED	-18	/* package already installed */
#define RPMERR_CHOWN		-19	/* chown() call failed */
#define RPMERR_NOUSER		-20	/* user does not exist */
#define RPMERR_NOGROUP		-21	/* group does not exist */
#define RPMERR_MKDIR		-22	/* mkdir() call failed */
#define RPMERR_FILECONFLICT     -23     /* file being installed exists */
#define RPMERR_RPMRC		-24     /* bad line in rpmrc */
#define RPMERR_NOSPEC		-25     /* .spec file is missing */
#define RPMERR_NOTSRPM		-26     /* a source rpm was expected */
#define RPMERR_FLOCK		-27     /* locking the database failed */
#define RPMERR_OLDPACKAGE	-28	/* trying upgrading to old version */
#define RPMERR_BADARCH          -29     /* bad architecture or arch mismatch */
#define RPMERR_CREATE		-30	/* failed to create a file */
#define RPMERR_NOSPACE		-31	/* out of disk space */
#define RPMERR_NORELOCATE	-32	/* tried to relocate improper package */
#define RPMERR_BADOS            -33     /* bad architecture or arch mismatch */
#define RPMMESS_BACKUP          -34     /* backup made during [un]install */

/* spec.c build.c pack.c */
#define RPMERR_UNMATCHEDIF      -107    /* unclosed %ifarch or %ifos */
#define RPMERR_BADARG           -109
#define RPMERR_SCRIPT           -110    /* errors related to script exec */
#define RPMERR_READERROR        -111
#define RPMERR_UNKNOWNOS        -112
#define RPMERR_UNKNOWNARCH      -113
#define RPMERR_EXEC             -114
#define RPMERR_FORK             -115
#define RPMERR_CPIO             -116
#define RPMERR_GZIP             -117
#define RPMERR_BADSPEC          -118
#define RPMERR_LDD              -119    /* couldn't understand ldd output */

#define RPMERR_BADSIGTYPE       -200    /* Unknown signature type */
#define RPMERR_SIGGEN           -201    /* Error generating signature */

/** signature.c **/

/**************************************************/
/*                                                */
/* Signature Tags                                 */
/*                                                */
/* These go in the sig Header to specify          */
/* individual signature types.                    */
/*                                                */
/**************************************************/

#define RPMSIGTAG_SIZE         	        1000
/* the md5 sum was broken on big endian machines for a while */
#define RPMSIGTAG_LITTLEENDIANMD5	1001
#define RPMSIGTAG_PGP          	        1002
#define RPMSIGTAG_MD5		        1003

/**************************************************/
/*                                                */
/* verifySignature() results                      */
/*                                                */
/**************************************************/

/* verifySignature() results */
#define RPMSIG_OK        0
#define RPMSIG_UNKNOWN   1
#define RPMSIG_BAD       2
#define RPMSIG_NOKEY     3  /* Do not have the key to check this signature */

void rpmFreeSignature(Header h);

int rpmVerifySignature(char *file, int_32 sigTag, void *sig, int count,
		       char *result);

#endif
