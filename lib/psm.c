/** \ingroup rpmts payload
 * \file lib/psm.c
 * Package state machine to handle a package from a transaction set.
 */

#include "system.h"

#include <errno.h>

#include <rpm/rpmlib.h>		/* rpmvercmp and others */
#include <rpm/rpmmacro.h>
#include <rpm/rpmds.h>
#include <rpm/rpmts.h>
#include <rpm/rpmfileutil.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmstring.h>
#include <rpm/argv.h>

#include "lib/fsm.h"		/* XXX CPIO_FOO/FSM_FOO constants */
#include "lib/rpmchroot.h"
#include "lib/rpmfi_internal.h" /* XXX replaced/states... */
#include "lib/rpmte_internal.h"	/* XXX internal apis */
#include "lib/rpmdb_internal.h" /* rpmdbAdd/Remove */
#include "lib/rpmts_internal.h" /* rpmtsPlugins() etc */
#include "lib/rpmscript.h"
#include "lib/misc.h"

#include "lib/rpmplugins.h"

#include "debug.h"

typedef enum pkgStage_e {
    PSM_UNKNOWN		=  0,
    PSM_INIT		=  1,
    PSM_PRE		=  2,
    PSM_PROCESS		=  3,
    PSM_POST		=  4,
    PSM_UNDO		=  5,
    PSM_FINI		=  6,

    PSM_CREATE		= 17,
    PSM_DESTROY		= 23,

    PSM_TRIGGERS	= 54,
    PSM_IMMED_TRIGGERS	= 55,

    PSM_RPMDB_ADD	= 98,
    PSM_RPMDB_REMOVE	= 99

} pkgStage;

struct rpmpsm_s {
    rpmts ts;			/*!< transaction set */
    rpmte te;			/*!< current transaction element */
    rpmfiles files;		/*!< transaction element file info */
    const char * goalName;
    char * failedFile;
    int npkgs_installed;	/*!< No. of installed instances. */
    int scriptArg;		/*!< Scriptlet package arg. */
    rpmsenseFlags sense;	/*!< One of RPMSENSE_TRIGGER{PREIN,IN,UN,POSTUN}. */
    int countCorrection;	/*!< 0 if installing, -1 if removing. */
    rpmCallbackType what;	/*!< Callback type. */
    rpm_loff_t amount;		/*!< Callback amount. */
    rpm_loff_t total;		/*!< Callback total. */
    pkgGoal goal;
    pkgStage stage;		/*!< Current psm stage. */
    pkgStage nstage;		/*!< Next psm stage. */

    int nrefs;			/*!< Reference count. */
};

static rpmpsm rpmpsmNew(rpmts ts, rpmte te);
static rpmpsm rpmpsmFree(rpmpsm psm);
static rpmRC rpmpsmStage(rpmpsm psm, pkgStage stage);

/**
 * Adjust file states in database for files shared with this package:
 * currently either "replaced" or "wrong color".
 * @param psm		package state machine data
 * @return		0 always
 */
static rpmRC markReplacedFiles(const rpmpsm psm)
{
    const rpmts ts = psm->ts;
    rpmfs fs = rpmteGetFileStates(psm->te);
    sharedFileInfo replaced = rpmfsGetReplaced(fs);
    sharedFileInfo sfi;
    rpmdbMatchIterator mi;
    Header h;
    unsigned int * offsets;
    unsigned int prev;
    unsigned int num;

    if (!replaced)
	return RPMRC_OK;

    num = prev = 0;
    for (sfi = replaced; sfi; sfi=rpmfsNextReplaced(fs, sfi)) {
	if (prev && prev == sfi->otherPkg)
	    continue;
	prev = sfi->otherPkg;
	num++;
    }
    if (num == 0)
	return RPMRC_OK;

    offsets = xmalloc(num * sizeof(*offsets));
    offsets[0] = 0;
    num = prev = 0;
    for (sfi = replaced; sfi; sfi=rpmfsNextReplaced(fs, sfi)) {
	if (prev && prev == sfi->otherPkg)
	    continue;
	prev = sfi->otherPkg;
	offsets[num++] = sfi->otherPkg;
    }

    mi = rpmtsInitIterator(ts, RPMDBI_PACKAGES, NULL, 0);
    rpmdbAppendIterator(mi, offsets, num);
    rpmdbSetIteratorRewrite(mi, 1);

    sfi = replaced;
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	int modified;
	struct rpmtd_s secStates;
	modified = 0;

	if (!headerGet(h, RPMTAG_FILESTATES, &secStates, HEADERGET_MINMEM))
	    continue;
	
	prev = rpmdbGetIteratorOffset(mi);
	num = 0;
	while (sfi && sfi->otherPkg == prev) {
	    int ix = rpmtdSetIndex(&secStates, sfi->otherFileNum);
	    assert(ix != -1);

	    char *state = rpmtdGetChar(&secStates);
	    if (state && *state != sfi->rstate) {
		*state = sfi->rstate;
		if (modified == 0) {
		    /* Modified header will be rewritten. */
		    modified = 1;
		    rpmdbSetIteratorModified(mi, modified);
		}
		num++;
	    }
	    sfi=rpmfsNextReplaced(fs, sfi);
	}
	rpmtdFreeData(&secStates);
    }
    rpmdbFreeIterator(mi);
    free(offsets);

    return RPMRC_OK;
}

static int rpmlibDeps(Header h)
{
    rpmds req = rpmdsInit(rpmdsNew(h, RPMTAG_REQUIRENAME, 0));
    rpmds rpmlib = NULL;
    rpmdsRpmlib(&rpmlib, NULL);
    int rc = 1;
    char *nvr = NULL;
    while (rpmdsNext(req) >= 0) {
	if (!(rpmdsFlags(req) & RPMSENSE_RPMLIB))
	    continue;
	if (rpmdsSearch(rpmlib, req) < 0) {
	    if (!nvr) {
		nvr = headerGetAsString(h, RPMTAG_NEVRA);
		rpmlog(RPMLOG_ERR, _("Missing rpmlib features for %s:\n"), nvr);
	    }
	    rpmlog(RPMLOG_ERR, "\t%s\n", rpmdsDNEVR(req)+2);
	    rc = 0;
	}
    }
    rpmdsFree(req);
    rpmdsFree(rpmlib);
    free(nvr);
    return rc;
}

rpmRC rpmInstallSourcePackage(rpmts ts, FD_t fd,
		char ** specFilePtr, char ** cookie)
{
    Header h = NULL;
    rpmpsm psm = NULL;
    rpmte te = NULL;
    rpmRC rpmrc;
    int specix = -1;

    rpmrc = rpmReadPackageFile(ts, fd, NULL, &h);
    switch (rpmrc) {
    case RPMRC_NOTTRUSTED:
    case RPMRC_NOKEY:
    case RPMRC_OK:
	break;
    default:
	goto exit;
	break;
    }
    if (h == NULL)
	goto exit;

    rpmrc = RPMRC_FAIL; /* assume failure */

    if (!headerIsSource(h)) {
	rpmlog(RPMLOG_ERR, _("source package expected, binary found\n"));
	goto exit;
    }

    /* src.rpm install can require specific rpmlib features, check them */
    if (!rpmlibDeps(h))
	goto exit;

    specix = headerFindSpec(h);

    if (specix < 0) {
	rpmlog(RPMLOG_ERR, _("source package contains no .spec file\n"));
	goto exit;
    };

    if (rpmtsAddInstallElement(ts, h, NULL, 0, NULL)) {
	goto exit;
    }

    te = rpmtsElement(ts, 0);
    if (te == NULL) {	/* XXX can't happen */
	goto exit;
    }
    rpmteSetFd(te, fd);

    rpmteSetHeader(te, h);

    {
	/* set all files to be installed */
	rpmfs fs = rpmteGetFileStates(te);
	int fc = rpmfsFC(fs);
	for (int i = 0; i < fc; i++)
	    rpmfsSetAction(fs, i, FA_CREATE);
    }

    psm = rpmpsmNew(ts, te);
    psm->goal = PKG_INSTALL;

   	/* FIX: psm->fi->dnl should be owned. */
    if (rpmpsmStage(psm, PSM_PROCESS) == RPMRC_OK)
	rpmrc = RPMRC_OK;

    (void) rpmpsmStage(psm, PSM_FINI);
    rpmpsmFree(psm);

exit:
    if (rpmrc == RPMRC_OK && specix >= 0) {
	if (cookie)
	    *cookie = headerGetAsString(h, RPMTAG_COOKIE);
	if (specFilePtr) {
	    rpmfiles files = rpmteFiles(te);
	    *specFilePtr = rpmfilesFN(files, specix);
	    rpmfilesFree(files);
	}
    }

    /* XXX nuke the added package(s). */
    headerFree(h);
    rpmtsEmpty(ts);

    return rpmrc;
}

static rpmTagVal triggertag(rpmsenseFlags sense) 
{
    rpmTagVal tag = RPMTAG_NOT_FOUND;
    switch (sense) {
    case RPMSENSE_TRIGGERIN:
	tag = RPMTAG_TRIGGERIN;
	break;
    case RPMSENSE_TRIGGERUN:
	tag = RPMTAG_TRIGGERUN;
	break;
    case RPMSENSE_TRIGGERPOSTUN:
	tag = RPMTAG_TRIGGERPOSTUN;
	break;
    case RPMSENSE_TRIGGERPREIN:
	tag = RPMTAG_TRIGGERPREIN;
	break;
    default:
	break;
    }
    return tag;
}

/**
 * Run a scriptlet with args.
 *
 * Run a script with an interpreter. If the interpreter is not specified,
 * /bin/sh will be used. If the interpreter is /bin/sh, then the args from
 * the header will be ignored, passing instead arg1 and arg2.
 *
 * @param psm		package state machine data
 * @param prefixes	install prefixes
 * @param script	scriptlet from header
 * @param arg1		no. instances of package installed after scriptlet exec
 *			(-1 is no arg)
 * @param arg2		ditto, but for the target package
 * @return		0 on success
 */
static rpmRC runScript(rpmpsm psm, ARGV_const_t prefixes, 
		       rpmScript script, int arg1, int arg2)
{
    rpmRC stoprc, rc = RPMRC_OK;
    rpmTagVal stag = rpmScriptTag(script);
    FD_t sfd = NULL;
    int warn_only = (stag != RPMTAG_PREIN &&
		     stag != RPMTAG_PREUN &&
		     stag != RPMTAG_PRETRANS &&
		     stag != RPMTAG_VERIFYSCRIPT);

    sfd = rpmtsNotify(psm->ts, psm->te, RPMCALLBACK_SCRIPT_START, stag, 0);
    if (sfd == NULL)
	sfd = rpmtsScriptFd(psm->ts);

    rpmswEnter(rpmtsOp(psm->ts, RPMTS_OP_SCRIPTLETS), 0);
    rc = rpmScriptRun(script, arg1, arg2, sfd,
		      prefixes, warn_only, rpmtsPlugins(psm->ts));
    rpmswExit(rpmtsOp(psm->ts, RPMTS_OP_SCRIPTLETS), 0);

    /* Map warn-only errors to "notfound" for script stop callback */
    stoprc = (rc != RPMRC_OK && warn_only) ? RPMRC_NOTFOUND : rc;
    rpmtsNotify(psm->ts, psm->te, RPMCALLBACK_SCRIPT_STOP, stag, stoprc);

    /* 
     * Notify callback for all errors. "total" abused for warning/error,
     * rc only reflects whether the condition prevented install/erase 
     * (which is only happens with %prein and %preun scriptlets) or not.
     */
    if (rc != RPMRC_OK) {
	if (warn_only) {
	    rc = RPMRC_OK;
	}
	rpmtsNotify(psm->ts, psm->te, RPMCALLBACK_SCRIPT_ERROR, stag, rc);
    }

    return rc;
}

static rpmRC runInstScript(rpmpsm psm, rpmTagVal scriptTag)
{
    rpmRC rc = RPMRC_OK;
    struct rpmtd_s pfx;
    Header h = rpmteHeader(psm->te);
    rpmScript script = rpmScriptFromTag(h, scriptTag);

    if (script) {
	headerGet(h, RPMTAG_INSTPREFIXES, &pfx, HEADERGET_ALLOC|HEADERGET_ARGV);
	rc = runScript(psm, pfx.data, script, psm->scriptArg, -1);
	rpmtdFreeData(&pfx);
    }

    rpmScriptFree(script);
    headerFree(h);

    return rc;
}

/**
 * Execute triggers.
 * @todo Trigger on any provides, not just package NVR.
 * @param psm		package state machine data
 * @param sourceH	header of trigger source
 * @param trigH		header of triggered package
 * @param arg2
 * @param triggersAlreadyRun
 * @return
 */
static rpmRC handleOneTrigger(const rpmpsm psm,
			Header sourceH, Header trigH,
			int arg2, unsigned char * triggersAlreadyRun)
{
    const rpmts ts = psm->ts;
    rpmds trigger = rpmdsInit(rpmdsNew(trigH, RPMTAG_TRIGGERNAME, 0));
    struct rpmtd_s pfx;
    const char * sourceName = headerGetString(sourceH, RPMTAG_NAME);
    const char * triggerName = headerGetString(trigH, RPMTAG_NAME);
    rpmRC rc = RPMRC_OK;
    int i;

    if (trigger == NULL)
	return rc;

    headerGet(trigH, RPMTAG_INSTPREFIXES, &pfx, HEADERGET_ALLOC|HEADERGET_ARGV);
    (void) rpmdsSetNoPromote(trigger, 1);

    while ((i = rpmdsNext(trigger)) >= 0) {
	struct rpmtd_s tindexes;
	uint32_t tix;

	if (!(rpmdsFlags(trigger) & psm->sense))
	    continue;

 	if (!rstreq(rpmdsN(trigger), sourceName))
	    continue;

	/* XXX Trigger on any provided dependency, not just the package NEVR */
	if (!rpmdsAnyMatchesDep(sourceH, trigger, 1))
	    continue;

	if (!headerGet(trigH, RPMTAG_TRIGGERINDEX, &tindexes, HEADERGET_MINMEM))
	    continue;

	if (rpmtdSetIndex(&tindexes, i) < 0) {
	    rpmtdFreeData(&tindexes);
	    continue;
	}

	tix = rpmtdGetNumber(&tindexes);
	if (triggersAlreadyRun == NULL || triggersAlreadyRun[tix] == 0) {
	    int arg1 = rpmdbCountPackages(rpmtsGetRdb(ts), triggerName);

	    if (arg1 < 0) {
		/* XXX W2DO? fails as "execution of script failed" */
		rc = RPMRC_FAIL;
	    } else {
		rpmScript script = rpmScriptFromTriggerTag(trigH,
						 triggertag(psm->sense), tix);
		arg1 += psm->countCorrection;
		rc = runScript(psm, pfx.data, script, arg1, arg2);

		if (triggersAlreadyRun != NULL)
		    triggersAlreadyRun[tix] = 1;

		rpmScriptFree(script);
	    }
	}

	rpmtdFreeData(&tindexes);

	/*
	 * Each target/source header pair can only result in a single
	 * script being run.
	 */
	break;
    }

    rpmtdFreeData(&pfx);
    rpmdsFree(trigger);

    return rc;
}

/**
 * Run trigger scripts in the database that are fired by this header.
 * @param psm		package state machine data
 * @return		0 on success
 */
static rpmRC runTriggers(rpmpsm psm)
{
    const rpmts ts = psm->ts;
    int numPackage = -1;
    const char * N = NULL;
    int nerrors = 0;

    if (psm->te) 	/* XXX can't happen */
	N = rpmteN(psm->te);
    if (N) 		/* XXX can't happen */
	numPackage = rpmdbCountPackages(rpmtsGetRdb(ts), N)
				+ psm->countCorrection;
    if (numPackage < 0)
	return RPMRC_NOTFOUND;

    {	Header triggeredH;
	Header h = rpmteHeader(psm->te);
	rpmdbMatchIterator mi;
	int countCorrection = psm->countCorrection;

	psm->countCorrection = 0;
	mi = rpmtsInitIterator(ts, RPMDBI_TRIGGERNAME, N, 0);
	while((triggeredH = rpmdbNextIterator(mi)) != NULL)
	    nerrors += handleOneTrigger(psm, h, triggeredH, numPackage, NULL);
	rpmdbFreeIterator(mi);
	psm->countCorrection = countCorrection;
	headerFree(h);
    }

    return (nerrors == 0) ? RPMRC_OK : RPMRC_FAIL;
}

/**
 * Run triggers from this header that are fired by headers in the database.
 * @param psm		package state machine data
 * @return		0 on success
 */
static rpmRC runImmedTriggers(rpmpsm psm)
{
    const rpmts ts = psm->ts;
    unsigned char * triggersRun;
    struct rpmtd_s tnames, tindexes;
    Header h = rpmteHeader(psm->te);
    int nerrors = 0;

    if (!(headerGet(h, RPMTAG_TRIGGERNAME, &tnames, HEADERGET_MINMEM) &&
	  headerGet(h, RPMTAG_TRIGGERINDEX, &tindexes, HEADERGET_MINMEM))) {
	goto exit;
    }

    triggersRun = xcalloc(rpmtdCount(&tindexes), sizeof(*triggersRun));
    {	Header sourceH = NULL;
	const char *trigName;
    	rpm_count_t *triggerIndices = tindexes.data;

	while ((trigName = rpmtdNextString(&tnames))) {
	    rpmdbMatchIterator mi;
	    int i = rpmtdGetIndex(&tnames);

	    if (triggersRun[triggerIndices[i]] != 0) continue;
	
	    mi = rpmtsInitIterator(ts, RPMDBI_NAME, trigName, 0);

	    while((sourceH = rpmdbNextIterator(mi)) != NULL) {
		nerrors += handleOneTrigger(psm, sourceH, h,
				rpmdbGetIteratorCount(mi),
				triggersRun);
	    }

	    rpmdbFreeIterator(mi);
	}
    }
    rpmtdFreeData(&tnames);
    rpmtdFreeData(&tindexes);
    free(triggersRun);

exit:
    headerFree(h);
    return (nerrors == 0) ? RPMRC_OK : RPMRC_FAIL;
}

static rpmpsm rpmpsmFree(rpmpsm psm)
{
    if (psm) {
	rpmfilesFree(psm->files);
	rpmtsFree(psm->ts),
	/* XXX rpmte not refcounted yet */
	memset(psm, 0, sizeof(*psm)); /* XXX trash and burn */
    	free(psm);
    }
    return NULL;
}

static rpmpsm rpmpsmNew(rpmts ts, rpmte te)
{
    rpmpsm psm = xcalloc(1, sizeof(*psm));
    psm->ts = rpmtsLink(ts);
    psm->files = rpmteFiles(te);
    psm->te = te; /* XXX rpmte not refcounted yet */
    return psm;
}

void rpmpsmNotify(rpmpsm psm, int what, rpm_loff_t amount)
{
    if (psm) {
	int changed = 0;
	if (amount > psm->amount) {
	    psm->amount = amount;
	    changed = 1;
	}
	if (what && what != psm->what) {
	    psm->what = what;
	    changed = 1;
	}
	if (changed) {
	   rpmtsNotify(psm->ts, psm->te, psm->what, psm->amount, psm->total);
	}
    }
}

/*
 * --replacepkgs hack: find the header instance we're replacing and
 * mark it as the db instance of the install element. In PSM_POST,
 * if an install element already has a db instance, it's removed
 * before proceeding with the adding the new header to the db.
 */
static void markReplacedInstance(rpmts ts, rpmte te)
{
    rpmdbMatchIterator mi = rpmtsInitIterator(ts, RPMDBI_NAME, rpmteN(te), 0);
    rpmdbSetIteratorRE(mi, RPMTAG_EPOCH, RPMMIRE_STRCMP, rpmteE(te));
    rpmdbSetIteratorRE(mi, RPMTAG_VERSION, RPMMIRE_STRCMP, rpmteV(te));
    rpmdbSetIteratorRE(mi, RPMTAG_RELEASE, RPMMIRE_STRCMP, rpmteR(te));
    /* XXX shouldn't we also do this on colorless transactions? */
    if (rpmtsColor(ts)) {
	rpmdbSetIteratorRE(mi, RPMTAG_ARCH, RPMMIRE_STRCMP, rpmteA(te));
	rpmdbSetIteratorRE(mi, RPMTAG_OS, RPMMIRE_STRCMP, rpmteO(te));
    }

    while (rpmdbNextIterator(mi) != NULL) {
	rpmteSetDBInstance(te, rpmdbGetIteratorOffset(mi));
	break;
    }
    rpmdbFreeIterator(mi);
}

static rpmRC rpmpsmNext(rpmpsm psm, pkgStage nstage)
{
    psm->nstage = nstage;
    return rpmpsmStage(psm, psm->nstage);
}

static rpmRC rpmpsmStage(rpmpsm psm, pkgStage stage)
{
    const rpmts ts = psm->ts;
    int fc = rpmfilesFC(psm->files);
    rpmRC rc = RPMRC_OK;

    switch (stage) {
    case PSM_UNKNOWN:
	break;
    case PSM_INIT:
	rpmlog(RPMLOG_DEBUG, "%s: %s has %d files\n",
		psm->goalName, rpmteNEVR(psm->te), fc);

	/*
	 * When we run scripts, we pass an argument which is the number of
	 * versions of this package that will be installed when we are
	 * finished.
	 */
	psm->npkgs_installed = rpmdbCountPackages(rpmtsGetRdb(ts), rpmteN(psm->te));
	if (psm->npkgs_installed < 0) {
	    rc = RPMRC_FAIL;
	    break;
	}

	if (psm->goal == PKG_INSTALL) {
	    Header h = rpmteHeader(psm->te);
	    psm->scriptArg = psm->npkgs_installed + 1;

	    psm->amount = 0;
	    psm->total = headerGetNumber(h, RPMTAG_LONGARCHIVESIZE);
	    /* fake up something for packages with no files */
	    if (psm->total == 0)
		psm->total = 100;

	    /* HACK: reinstall abuses te instance to remove old header */
	    if (rpmtsFilterFlags(ts) & RPMPROB_FILTER_REPLACEPKG)
		markReplacedInstance(ts, psm->te);

	    headerFree(h);
	}
	if (psm->goal == PKG_ERASE) {
	    psm->scriptArg = psm->npkgs_installed - 1;

	    psm->amount = 0;
	    psm->total = fc ? fc : 100;
	}
	break;
    case PSM_PRE:
	if (psm->goal == PKG_INSTALL) {
	    psm->sense = RPMSENSE_TRIGGERPREIN;
	    psm->countCorrection = 0;   /* XXX is this correct?!? */

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERPREIN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmNext(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmNext(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPRE)) {
		rc = runInstScript(psm, RPMTAG_PREIN);
		if (rc) break;
	    }
	}

	if (psm->goal == PKG_ERASE) {
	    psm->sense = RPMSENSE_TRIGGERUN;
	    psm->countCorrection = -1;

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERUN)) {
		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmNext(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;

		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmNext(psm, PSM_TRIGGERS);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPREUN))
		rc = runInstScript(psm, RPMTAG_PREUN);
	}
	break;
    case PSM_PROCESS:
	if (psm->goal == PKG_INSTALL) {
	    int fsmrc = 0;
	    int saved_errno = 0;

	    rpmpsmNotify(psm, RPMCALLBACK_INST_START, 0);
	    /* make sure first progress call gets made */
	    rpmpsmNotify(psm, RPMCALLBACK_INST_PROGRESS, 0);

	    if (fc > 0 && !(rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)) {
		FD_t payload = rpmtePayload(psm->te);
		if (payload == NULL) {
		    rc = RPMRC_FAIL;
		    break;
		}

		fsmrc = rpmPackageFilesInstall(psm->ts, psm->te, psm->files,
				  payload, psm, &psm->failedFile);
		saved_errno = errno;

		rpmswAdd(rpmtsOp(psm->ts, RPMTS_OP_UNCOMPRESS),
			 fdOp(payload, FDSTAT_READ));
		rpmswAdd(rpmtsOp(psm->ts, RPMTS_OP_DIGEST),
			 fdOp(payload, FDSTAT_DIGEST));

		Fclose(payload);
	    }

	    /* XXX make sure progress reaches 100% */
	    rpmpsmNotify(psm, RPMCALLBACK_INST_PROGRESS, psm->total);
	    rpmpsmNotify(psm, RPMCALLBACK_INST_STOP, psm->total);

	    if (fsmrc) {
		char *emsg;
		errno = saved_errno;
		emsg = rpmfileStrerror(fsmrc);
		rpmlog(RPMLOG_ERR,
			_("unpacking of archive failed%s%s: %s\n"),
			(psm->failedFile != NULL ? _(" on file ") : ""),
			(psm->failedFile != NULL ? psm->failedFile : ""),
			emsg);
		free(emsg);
		rc = RPMRC_FAIL;

		/* XXX notify callback on error. */
		rpmtsNotify(ts, psm->te, RPMCALLBACK_UNPACK_ERROR, 0, 0);
		break;
	    }
	}
	if (psm->goal == PKG_ERASE) {
	    if (rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)	break;

	    rpmpsmNotify(psm, RPMCALLBACK_UNINST_START, 0);
	    /* make sure first progress call gets made */
	    rpmpsmNotify(psm, RPMCALLBACK_UNINST_PROGRESS, 0);

	    /* XXX should't we log errors from here? */
	    if (fc > 0 && !(rpmtsFlags(ts) & RPMTRANS_FLAG_JUSTDB)) {
		rc = rpmPackageFilesRemove(psm->ts, psm->te, psm->files,
				  psm, &psm->failedFile);
	    }

	    /* XXX make sure progress reaches 100% */
	    rpmpsmNotify(psm, RPMCALLBACK_UNINST_PROGRESS, psm->total);
	    rpmpsmNotify(psm, RPMCALLBACK_UNINST_STOP, psm->total);
	}
	break;
    case PSM_POST:
	if (psm->goal == PKG_INSTALL) {
	    rpm_time_t installTime = (rpm_time_t) time(NULL);
	    rpmfs fs = rpmteGetFileStates(psm->te);
	    rpm_count_t fc = rpmfsFC(fs);
	    rpm_fstate_t * fileStates = rpmfsGetStates(fs);
	    Header h = rpmteHeader(psm->te);
	    rpm_color_t tscolor = rpmtsColor(ts);

	    if (fileStates != NULL && fc > 0) {
		headerPutChar(h, RPMTAG_FILESTATES, fileStates, fc);
	    }

	    headerPutUint32(h, RPMTAG_INSTALLTIME, &installTime, 1);
	    headerPutUint32(h, RPMTAG_INSTALLCOLOR, &tscolor, 1);
	    headerFree(h);

	    /*
	     * If this package has already been installed, remove it from
	     * the database before adding the new one.
	     */
	    if (rpmteDBInstance(psm->te)) {
		rc = rpmpsmNext(psm, PSM_RPMDB_REMOVE);
		if (rc) break;
	    }

	    rc = rpmpsmNext(psm, PSM_RPMDB_ADD);
	    if (rc) break;

	    psm->sense = RPMSENSE_TRIGGERIN;
	    psm->countCorrection = 0;

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPOST)) {
		rc = runInstScript(psm, RPMTAG_POSTIN);
		if (rc) break;
	    }
	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERIN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmNext(psm, PSM_TRIGGERS);
		if (rc) break;

		/* Run triggers in this package other package(s) set off. */
		rc = rpmpsmNext(psm, PSM_IMMED_TRIGGERS);
		if (rc) break;
	    }

	    rc = markReplacedFiles(psm);

	}
	if (psm->goal == PKG_ERASE) {

	    psm->sense = RPMSENSE_TRIGGERPOSTUN;
	    psm->countCorrection = -1;

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOPOSTUN)) {
		rc = runInstScript(psm, RPMTAG_POSTUN);
		if (rc) break;
	    }

	    if (!(rpmtsFlags(ts) & RPMTRANS_FLAG_NOTRIGGERPOSTUN)) {
		/* Run triggers in other package(s) this package sets off. */
		rc = rpmpsmNext(psm, PSM_TRIGGERS);
		if (rc) break;
	    }

	    rc = rpmpsmNext(psm, PSM_RPMDB_REMOVE);
	}
	break;
    case PSM_UNDO:
	break;
    case PSM_FINI:
	if (rc) {
	    char *emsg = rpmfileStrerror(rc);
	    if (psm->failedFile)
		rpmlog(RPMLOG_ERR,
			_("%s failed on file %s: %s\n"),
			psm->goalName, psm->failedFile, emsg);
	    else
		rpmlog(RPMLOG_ERR, _("%s failed: %s\n"),
			psm->goalName, emsg);
	    free(emsg);

	    /* XXX notify callback on error. */
	    rpmtsNotify(ts, psm->te, RPMCALLBACK_CPIO_ERROR, 0, 0);
	}

	psm->failedFile = _free(psm->failedFile);

	break;

    case PSM_CREATE:
	break;
    case PSM_DESTROY:
	break;
    case PSM_TRIGGERS:
	/* Run triggers in other package(s) this package sets off. */
	rc = runTriggers(psm);
	break;
    case PSM_IMMED_TRIGGERS:
	/* Run triggers in this package other package(s) set off. */
	rc = runImmedTriggers(psm);
	break;

    case PSM_RPMDB_ADD: {
	Header h = rpmteHeader(psm->te);

	if (!headerIsEntry(h, RPMTAG_INSTALLTID)) {
	    rpm_tid_t tid = rpmtsGetTid(ts);
	    if (tid != 0 && tid != (rpm_tid_t)-1)
		headerPutUint32(h, RPMTAG_INSTALLTID, &tid, 1);
	}
	
	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DBADD), 0);
	rc = (rpmdbAdd(rpmtsGetRdb(ts), h) == 0) ? RPMRC_OK : RPMRC_FAIL;
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DBADD), 0);

	if (rc == RPMRC_OK)
	    rpmteSetDBInstance(psm->te, headerGetInstance(h));
	headerFree(h);
    }   break;

    case PSM_RPMDB_REMOVE:
	(void) rpmswEnter(rpmtsOp(ts, RPMTS_OP_DBREMOVE), 0);
	rc = (rpmdbRemove(rpmtsGetRdb(ts), rpmteDBInstance(psm->te)) == 0) ?
						    RPMRC_OK : RPMRC_FAIL;
	(void) rpmswExit(rpmtsOp(ts, RPMTS_OP_DBREMOVE), 0);
	if (rc == RPMRC_OK)
	    rpmteSetDBInstance(psm->te, 0);
	break;

    default:
	break;
   }

    return rc;
}

static const char * pkgGoalString(pkgGoal goal)
{
    switch(goal) {
    case PKG_INSTALL:	return "  install";
    case PKG_ERASE:	return "    erase";
    case PKG_VERIFY:	return "   verify";
    case PKG_PRETRANS:	return " pretrans";
    case PKG_POSTTRANS:	return "posttrans";
    default:		return "unknown";
    }
}

rpmRC rpmpsmRun(rpmts ts, rpmte te, pkgGoal goal)
{
    rpmpsm psm = NULL;
    rpmRC rc = RPMRC_FAIL;

    /* Psm can't fail in test mode, just return early */
    if (rpmtsFlags(ts) & RPMTRANS_FLAG_TEST)
	return RPMRC_OK;

    psm = rpmpsmNew(ts, te);
    if (rpmChrootIn() == 0) {
	rpmtsOpX op;
	psm->goal = goal;
	psm->goalName = pkgGoalString(goal);

	switch (goal) {
	case PKG_INSTALL:
	case PKG_ERASE:
	    /* Run pre transaction element hook for all plugins */
	    if (rpmpluginsCallPsmPre(rpmtsPlugins(ts), te) != RPMRC_FAIL) {

		op = (goal == PKG_INSTALL) ? RPMTS_OP_INSTALL : RPMTS_OP_ERASE;
		rpmswEnter(rpmtsOp(psm->ts, op), 0);

		rc = rpmpsmNext(psm, PSM_INIT);
		if (!rc) rc = rpmpsmNext(psm, PSM_PRE);
		if (!rc) rc = rpmpsmNext(psm, PSM_PROCESS);
		if (!rc) rc = rpmpsmNext(psm, PSM_POST);
		(void) rpmpsmNext(psm, PSM_FINI);

		rpmswExit(rpmtsOp(psm->ts, op), 0);
	    }

	    /* Run post transaction element hook for all plugins */
	    rpmpluginsCallPsmPost(rpmtsPlugins(ts), te, rc);
	    break;
	case PKG_PRETRANS:
	case PKG_POSTTRANS:
	case PKG_VERIFY:
	    rc = runInstScript(psm, goal);
	    break;
	default:
	    break;
	}
	/* XXX an error here would require a full abort */
	(void) rpmChrootOut();
    }
    rpmpsmFree(psm);
    return rc;
}
