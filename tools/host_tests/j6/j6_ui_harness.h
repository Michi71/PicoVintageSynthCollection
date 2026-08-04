/*
  j6_ui_harness.h -- driving J6_Controller from a test

  The panel is three encoders and a button, and every one of them wraps. That
  makes "go to page 2" and "select patch 40" surprisingly easy to get wrong, and
  four separate test bugs in this project were exactly that:

      * onEncoder1(section - cursor) written as a forward-only loop, which does
        nothing at all when the target is behind the cursor
      * twenty steps backwards through eleven sections, which wraps to nine
      * listCount() read at the top level, where it counts sections and not
        patches
      * pages addressed by index, so inserting PATCH NAME moved PATCH WRITE and
        two older tests silently drove the wrong page

  Every one of them looked like a firmware fault first. So the rule here is that
  nothing is addressed by number: sections and pages are found by their name,
  the patch list by its entry, and the cursor by reading back where it actually
  is. A helper that cannot find its target says so and fails the test rather
  than carrying on somewhere else.

  Single translation unit -- include it once, from the test.
*/

#ifndef J6_UI_HARNESS_H
#define J6_UI_HARNESS_H

#include "J6_Controller.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

/* ---------------------------------------------------------------- results - */
static int  g_failures = 0;
static void ck(bool ok, const char* what)
{
    printf("  %-52s %s\n", what, ok ? "ok" : "FEHLER");
    if (!ok) ++g_failures;
}
static void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("      "); vprintf(fmt, ap); printf("\n");
    va_end(ap);
}
static void group(const char* title) { printf("\n%s\n", title); }

/* ------------------------------------------------------------ navigation - */

/* The section list is the only screen whose title is fixed, so it doubles as
 * "am I at the top level". */
static bool atTopLevel(J6_Controller& c) { return strcmp(c.title(), "MENU") == 0; }

static void toTopLevel(J6_Controller& c) { if (!atTopLevel(c)) c.onSelectButton(); }

/*
 * Move to a section by its printed name and step into it. One call to
 * onEncoder1 with the whole distance: the controller wraps that correctly,
 * whereas walking it a detent at a time has to know which way round to go.
 */
static bool goToSection(J6_Controller& c, const char* name)
{
    toTopLevel(c);

    const int n = c.listCount();
    for (int i = 0; i < n; ++i) {
        char entry[32];
        c.listEntry(c.listCursor(), entry, sizeof(entry));
        if (strcmp(entry, name) == 0) { c.onSelectButton(); return true; }
        c.onEncoder1(1);
    }

    printf("  Sektion \"%s\" nicht gefunden\n", name);
    ++g_failures;
    return false;
}

/* Move to a page by its title. Titles and not indices, so inserting a page
 * cannot silently redirect a test to its neighbour. */
static bool goToPage(J6_Controller& c, const char* section, const char* page)
{
    if (!goToSection(c, section)) return false;

    for (int i = 0; i < 16; ++i) {
        if (strcmp(c.title(), page) == 0) return true;
        c.onEncoder1(1);
    }

    printf("  Seite \"%s\" in \"%s\" nicht gefunden\n", page, section);
    ++g_failures;
    return false;
}

/* --------------------------------------------------------------- patches - */

/* Jump straight to a patch. One call, so nothing in between is loaded -- use
 * stepPatch when the loading on the way there is the point. */
static void selectPatch(J6_Controller& c, int index)
{
    goToPage(c, "PATCH", "PATCH");
    c.onEncoder2(index - c.listCursor());
}

/* One detent at a time, the way the hardware delivers them: every entry the
 * cursor passes over is loaded. */
static void stepPatch(J6_Controller& c, int detents)
{
    goToPage(c, "PATCH", "PATCH");
    const int dir = detents < 0 ? -1 : 1;
    for (int i = 0; i < (detents < 0 ? -detents : detents); ++i) c.onEncoder2(dir);
}

/*
 * The action, read back rather than counted. In write mode the line carries the
 * outgoing name, so anything that is not "Erase >" is write.
 */
static void setWriteAction(J6_Controller& c, bool erase)
{
    char shown[24];
    c.paramBText(shown, sizeof(shown));
    if ((strcmp(shown, "Erase >") == 0) != erase) c.onEncoder3(1);
}

/*
 * The write destination -- and the action along with it.
 *
 * Setting both is what makes a group independent of the ones before it: the
 * action is sticky, so a test that left it on Erase silently turned every
 * later runPatchAction() in the suite into a delete. That cost a run of eleven
 * failures that all looked like the store had broken.
 */
static bool setWriteTarget(J6_Controller& c, int slot, bool erase = false)
{
    if (!goToPage(c, "PATCH", "PATCH WRITE")) return false;

    for (int i = 0; i < J6_USER_PATCHES + 1; ++i) {
        char t[32];
        c.paramAText(t, sizeof(t));
        int shown = 0;
        if (sscanf(t, "U%d", &shown) == 1 && shown - 1 == slot) {
            setWriteAction(c, erase);
            return true;
        }
        c.onEncoder2(1);
    }

    printf("  Speicherplatz U%02d nicht erreichbar\n", slot + 1);
    ++g_failures;
    return false;
}

/* ------------------------------------------------------------------ names - */

/* The order the character encoder steps through -- kept in step with
 * kNameChars in J6_Controller.cpp. */
static const char kHarnessNameChars[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-+'/&#";

/* Where the cursor is, read out of the bracket the page draws around it. */
static int namePos(J6_Controller& c)
{
    char t[40];
    c.paramAText(t, sizeof(t));
    for (int i = 0; t[i]; ++i) if (t[i] == '[') return i;
    return -1;
}

static void cursorTo(J6_Controller& c, int pos)
{
    if (!goToPage(c, "PATCH", "PATCH NAME")) return;
    const int here = namePos(c);
    if (here < 0) { printf("  kein Cursor auf der NAME-Seite\n"); ++g_failures; return; }
    c.onEncoder2(pos - here);
}

static void typeAt(J6_Controller& c, int pos, char want)
{
    cursorTo(c, pos);

    char shown[16];
    c.paramBText(shown, sizeof(shown));
    const char cur = strcmp(shown, "space") ? shown[0] : ' ';

    const char* from = strchr(kHarnessNameChars, cur);
    const char* to   = strchr(kHarnessNameChars, want);
    if (!from || !to) { printf("  Zeichen nicht im Satz\n"); ++g_failures; return; }
    c.onEncoder3((int) (to - from));
}

/* Set the whole name, padding with spaces so leftovers of a longer one go. */
static void setName(J6_Controller& c, const char* name)
{
    const int len = (int) strlen(name);
    for (int i = 0; i < J6_NAME_EDIT_LEN; ++i)
        typeAt(c, i, i < len ? name[i] : ' ');
}

/* ---------------------------------------------------------------- display - */

/* The header exactly as j6_main composes it, marker and all. */
static void header(J6_Controller& c, char* dst, size_t n)
{
    snprintf(dst, n, "%s%s", c.isEdited() ? "*" : "", c.title());
}

/*
 * Both body lines, built the way j6_main builds them -- an empty label gives
 * the value the whole line. Anything past fifteen characters would be cut off
 * on the display, so it is a failure here rather than a surprise on the device.
 */
#define J6_LINE_CHARS 15

static void bodyLines(J6_Controller& c, char* a, size_t na, char* b, size_t nb)
{
    char va[24], vb[24];
    c.paramAText(va, sizeof(va));
    c.paramBText(vb, sizeof(vb));

    const char* la = c.paramAName();
    const char* lb = c.paramBName();

    if (la[0]) snprintf(a, na, "%s %s", la, va); else snprintf(a, na, "%s", va);
    if (lb[0]) snprintf(b, nb, "%s %s", lb, vb); else snprintf(b, nb, "%s", vb);

    if (strlen(a) > J6_LINE_CHARS || strlen(b) > J6_LINE_CHARS) {
        printf("  ZU LANG fuer die Zeile: \"%s\" / \"%s\"\n", a, b);
        ++g_failures;
    }
}

static void showScreen(J6_Controller& c, const char* tag)
{
    char h[24], n[12], a[40], b[40];
    header(c, h, sizeof(h));
    c.counterText(n, sizeof(n));
    bodyLines(c, a, sizeof(a), b, sizeof(b));
    printf("      %-24s [%s%*s%s]  %-16s | %s\n", tag, h,
           (int) (J6_LINE_CHARS + 1 - strlen(h) - strlen(n)), "", n, a, b);
}

#endif /* J6_UI_HARNESS_H */
