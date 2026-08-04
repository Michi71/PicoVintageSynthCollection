/*
  j6_ui_test.cpp -- the panel and the user memories, on the host

  Covers the parts of PicoFaceJ6 that are logic rather than sound: the two-level
  menu, the 56 user memories, naming, writing, freeing, and the marker that says
  a sound has been edited. The engine has its own self test in juno_test.cpp.

  Every group here exists because something was actually wrong once:

      list        the patch list ran to 48 where it should run to 104, so the
                  memories showed as blank rows
      write       setProgram never reached the memories; the destination was
                  read out of a function that only ran on the list page
      name        a memory saved after browsing for a free slot was called
                  "Init", because the name came from the cursor position rather
                  than from the sound that was loaded
      writepage   the outgoing name was not shown before writing, so a rename
                  could not be confirmed anywhere; and the last result stayed on
                  screen across pages, covering it up
      edited      no sign that turning the patch encoder was about to discard
                  work

  Built by build_ui.sh. No audio, no MIDI, no terminal -- it prints and exits.
*/

#include "j6_ui_harness.h"

/* The panel's MIDI object only matters here for the receive channel, which the
 * controller sets on it. Nothing in these tests reads it back. */
static J6_Midi midi;
void J6_Midi::setRxChannel(uint8_t) {}

/* ------------------------------------------------------------------------ */
static void test_list(J6_Controller& c)
{
    group("Die Patchliste fuehrt Werksklaenge und Speicher");

    ck(!j6_patch_valid(0), "Platz 1 ist frei");
    ck(j6_patch_name(0)[0] == 0, "ein freier Platz hat keinen Namen");

    goToPage(c, "PATCH", "PATCH");
    ck(c.listCount() == JUNO_NPROGRAMS + J6_USER_PATCHES,
       "die Liste hat 48 + 56 Eintraege");

    char e[32];
    c.listEntry(0, e, sizeof(e));
    ck(strncmp(e, " 1 Strings I", 12) == 0, "Eintrag 1 ist ein Werksklang");
    c.listEntry(JUNO_NPROGRAMS, e, sizeof(e));
    ck(strcmp(e, "U01 -free-") == 0, "Eintrag 49 ist Speicherplatz 1, frei");
}

/* ------------------------------------------------------------------------ */
static void test_write(J6_Controller& c)
{
    group("Schreiben, nur von der WRITE-Seite aus");

    selectPatch(c, 0);
    goToPage(c, "PATCH", "PATCH WRITE");
    showScreen(c, "vor dem Schreiben");
    ck(c.onWritePage(), "die WRITE-Seite ist erreicht");

    ck(c.runPatchAction(), "Schreiben gemeldet");
    ck(j6_patch_valid(0), "Platz 1 ist belegt");
    ck(strcmp(j6_patch_name(0), "Strings I") == 0,
       "Name vom geladenen Klang uebernommen");

    goToPage(c, "PATCH", "PATCH");
    ck(!c.onWritePage(), "auf der Listenseite schreibt der Taster nicht");
    ck(!c.runPatchAction(), "runPatchAction() lehnt dort ab");

    J6UserPatch up;
    ck(j6_patch_read(0, up), "Platz 1 ist lesbar");
    ck(sizeof(up.param) / sizeof(up.param[0]) == JUNO_PARAM_COUNT,
       "gespeichert wird nur die Patch-Haelfte");
}

/* ------------------------------------------------------------------------ */
static void test_roundtrip(J6_Controller& c)
{
    group("Bearbeiten, schreiben, zurueckladen");

    selectPatch(c, 8);                       /* Piano I */
    char loaded[32]; c.paramAText(loaded, sizeof(loaded));
    note("geladen: %s", loaded);

    goToPage(c, "VCF", "VCF");
    char before[24]; c.paramAText(before, sizeof(before));
    c.onEncoder2(-15);
    char edited[24]; c.paramAText(edited, sizeof(edited));
    note("Cutoff %s -> %s", before, edited);
    ck(strcmp(before, edited) != 0, "der Cutoff wurde veraendert");

    setWriteTarget(c, 1);
    ck(c.runPatchAction(), "auf Platz 2 geschrieben");
    ck(strcmp(j6_patch_name(1), "Piano I") == 0, "Name \"Piano I\" uebernommen");

    selectPatch(c, 0);
    goToPage(c, "VCF", "VCF");
    char other[24]; c.paramAText(other, sizeof(other));
    note("nach Werksklang 1: Cutoff %s", other);

    selectPatch(c, JUNO_NPROGRAMS + 1);
    goToPage(c, "VCF", "VCF");
    char back[24]; c.paramAText(back, sizeof(back));
    note("nach dem Zurueckladen: Cutoff %s", back);
    ck(strcmp(back, edited) == 0, "der bearbeitete Cutoff kam zurueck");
}

/* ------------------------------------------------------------------------ */
static void test_erase(J6_Controller& c)
{
    group("Loeschen gibt einen Platz wieder frei");

    setWriteTarget(c, 1, /*erase=*/true);
    showScreen(c, "Loeschen gewaehlt");

    char b[24]; c.paramBText(b, sizeof(b));
    ck(strcmp(b, "Erase >") == 0, "Encoder 3 schaltet auf Loeschen");
    ck(c.runPatchAction(), "Loeschen gemeldet");
    ck(!j6_patch_valid(1), "Platz 2 ist wieder frei");

    c.paramBText(b, sizeof(b));
    ck(strcmp(b, "freed") == 0, "Rueckmeldung \"freed\"");

    goToPage(c, "PATCH", "PATCH");
    char e[32]; c.listEntry(JUNO_NPROGRAMS + 1, e, sizeof(e));
    ck(strcmp(e, "U02 -free-") == 0, "die Patchliste zeigt ihn als frei");

    setWriteTarget(c, 1, /*erase=*/true);
    ck(c.runPatchAction(), "ein freier Platz laesst sich gefahrlos loeschen");
    ck(j6_patch_valid(0), "Platz 1 hat das unberuehrt ueberstanden");
}

/* ------------------------------------------------------------------------ */
static void test_name_lineage(J6_Controller& c)
{
    group("Der Name folgt dem Klang, nicht dem Listenzeiger");

    /* Genau der Weg, auf dem jeder Speicher "Init" hiess: in der Liste bis zu
     * einem freien Platz blaettern -- der laedt absichtlich nichts, der Klang
     * spielt weiter, und nach dessen Namen wurde nicht gefragt. */
    selectPatch(c, 4);                        /* Organ II */
    char sound[32]; c.paramAText(sound, sizeof(sound));
    note("Klang gewaehlt: %s", sound);

    selectPatch(c, JUNO_NPROGRAMS + 4);       /* U05, frei */
    char cursor[32]; c.paramAText(cursor, sizeof(cursor));
    note("durchgeblaettert bis: %s", cursor);

    setWriteTarget(c, 4);
    c.runPatchAction();
    note("U05 heisst \"%s\"", j6_patch_name(4));
    ck(strcmp(j6_patch_name(4), "Organ II") == 0,
       "der Name kommt vom geladenen Klang");

    selectPatch(c, JUNO_NPROGRAMS + 4);       /* jetzt belegt */
    setWriteTarget(c, 5);
    c.runPatchAction();
    ck(strcmp(j6_patch_name(5), "Organ II") == 0,
       "von einem belegten Platz aus wird er weitergereicht");
}

/* ------------------------------------------------------------------------ */
static void test_scrolling(J6_Controller& c)
{
    group("Blaettern laedt jeden Eintrag, den der Cursor passiert");

    /*
     * Eine Raste je Aufruf, so wie der Encoder sie liefert -- und damit der
     * Weg, auf dem man sich den Klang wegblaettert, den man speichern wollte:
     * von Patch 40 bis U01 sind es acht Werksklaenge, und der letzte davon ist
     * am Ende geladen. Das ist kein Fehler, sondern der Grund, warum das Ziel
     * auf der WRITE-Seite gewaehlt wird und nicht in der Liste.
     */
    selectPatch(c, 39);                       /* 40 PWM Chorus */
    char from[32]; c.paramAText(from, sizeof(from));
    note("geladen: %s", from);

    stepPatch(c, 9);                          /* bis U01, Raste fuer Raste */
    char at[32]; c.paramAText(at, sizeof(at));
    note("nach neun Rasten: %s", at);
    ck(strncmp(at, "U01", 3) == 0, "der Zeiger steht auf U01");

    goToPage(c, "PATCH", "PATCH WRITE");
    char a[40], b[40]; bodyLines(c, a, sizeof(a), b, sizeof(b));
    note("die WRITE-Seite zeigt: %s | %s", a, b);
    ck(strstr(b, "PWM Chorus") == nullptr,
       "und nennt nicht mehr den Klang, von dem aus geblaettert wurde");

    /* Der Weg, der taugt: Ziel auf der WRITE-Seite, Liste unberuehrt. */
    selectPatch(c, 39);
    setWriteTarget(c, 10);
    bodyLines(c, a, sizeof(a), b, sizeof(b));
    ck(strstr(b, "PWM Chorus >") != nullptr,
       "ueber die WRITE-Seite bleibt der geladene Klang stehen");
    ck(c.runPatchAction(), "auf U11 geschrieben");
    ck(strcmp(j6_patch_name(10), "PWM Chorus") == 0, "und dort liegt er auch");
}

/* ------------------------------------------------------------------------ */
static void test_naming(J6_Controller& c)
{
    group("Namen selbst vergeben");

    selectPatch(c, 0);
    goToPage(c, "PATCH", "PATCH NAME");
    showScreen(c, "frisch geladen");

    ck(c.paramAName()[0] == 0, "der Name hat kein Label, er nimmt die Zeile");
    ck(strcmp(c.paramBName(), "Char") == 0, "der zweite Wert heisst Char");

    char v[40];
    c.paramAText(v, sizeof(v));
    ck(strcmp(v, "[S]trings I") == 0, "Cursor auf dem ersten Zeichen");

    cursorTo(c, 4);
    c.paramAText(v, sizeof(v));
    ck(strcmp(v, "Stri[n]gs I") == 0, "Cursor laesst sich bewegen");

    cursorTo(c, 10);
    c.paramAText(v, sizeof(v));
    ck(strcmp(v, "Strings I [ ]") == 0, "hinter dem Ende bleiben Leerstellen sichtbar");
    c.paramBText(v, sizeof(v));
    ck(strcmp(v, "space") == 0, "Char zeigt dort space");

    setName(c, "Shine On");
    cursorTo(c, 0);
    c.paramAText(v, sizeof(v));
    note("getippt: \"%s\"", v);

    setWriteTarget(c, 2);
    ck(c.runPatchAction(), "auf U03 geschrieben");
    ck(strcmp(j6_patch_name(2), "Shine On") == 0, "der getippte Name kam an");

    goToPage(c, "PATCH", "PATCH");
    char e[32]; c.listEntry(JUNO_NPROGRAMS + 2, e, sizeof(e));
    ck(strcmp(e, "U03 Shine On") == 0, "die Patchliste zeigt ihn");

    selectPatch(c, 0);
    selectPatch(c, JUNO_NPROGRAMS + 2);
    cursorTo(c, 0);
    c.paramAText(v, sizeof(v));
    ck(strncmp(v, "[S]hine On", 10) == 0, "Zurueckladen uebernimmt den Namen");

    setName(c, "");
    setWriteTarget(c, 3);
    ck(c.runPatchAction(), "auf U04 geschrieben");
    ck(strcmp(j6_patch_name(3), "Init") == 0,
       "ein leergeraeumter Name wird als Init abgelegt");
}

/* ------------------------------------------------------------------------ */
static void test_truncation(J6_Controller& c)
{
    group("Zu lange Werksnamen lassen sich reparieren");

    int longest = -1;
    for (int i = 0; i < JUNO_NPROGRAMS; ++i)
        if (strcmp(junoPrograms[i].name, "Harpsichord I") == 0) { longest = i; break; }
    ck(longest >= 0, "Harpsichord I gefunden");
    if (longest < 0) return;

    selectPatch(c, longest);
    setWriteTarget(c, 6);
    c.runPatchAction();
    note("13 Zeichen abgelegt als \"%s\"", j6_patch_name(6));
    ck(strcmp(j6_patch_name(6), "Harpsichord") == 0,
       "das Feld kuerzt auf elf Zeichen");

    setName(c, "Harpsi I");
    setWriteTarget(c, 7);
    c.runPatchAction();
    ck(strcmp(j6_patch_name(7), "Harpsi I") == 0, "und laesst sich neu vergeben");
}

/* ------------------------------------------------------------------------ */
static void test_write_page(J6_Controller& c)
{
    group("Die WRITE-Seite zeigt, was hineingeschrieben wird");

    selectPatch(c, 0);
    setWriteTarget(c, 8);
    setName(c, "Test");

    goToPage(c, "PATCH", "PATCH WRITE");
    showScreen(c, "nach dem Umbenennen");

    char a[40], b[40];
    bodyLines(c, a, sizeof(a), b, sizeof(b));
    ck(strstr(b, "Test >") != nullptr,
       "der neue Name steht schon vor dem Schreiben da");
    ck(strstr(a, "U09") != nullptr && strstr(a, "-free-") != nullptr,
       "die obere Zeile zeigt weiter, was ueberschrieben wuerde");

    c.runPatchAction();
    bodyLines(c, a, sizeof(a), b, sizeof(b));
    ck(strstr(a, "U09 Test") != nullptr, "danach steht der neue Inhalt oben");
    ck(strstr(b, "written") != nullptr, "und die Rueckmeldung unten");

    /* Die Rueckmeldung darf nicht ueber eine andere Seite hinweg stehenbleiben
     * -- sonst verdeckt sie beim Zurueckkommen genau den Namen, den sie
     * bestaetigen soll. */
    setName(c, "Zwei");
    goToPage(c, "PATCH", "PATCH WRITE");
    bodyLines(c, a, sizeof(a), b, sizeof(b));
    ck(strstr(b, "Zwei >") != nullptr,
       "nach einem Umweg ueber die NAME-Seite ist sie geraeumt");

    /* Der laengste moegliche Name plus Pfeil muss noch in die Zeile passen --
     * bodyLines schlaegt selbst an, wenn nicht. */
    setName(c, "Synthetiser");
    goToPage(c, "PATCH", "PATCH WRITE");
    bodyLines(c, a, sizeof(a), b, sizeof(b));
    ck(strcmp(b, "Synthetiser >") == 0, "elf Zeichen plus Pfeil passen");

    setWriteAction(c, /*erase=*/true);
    bodyLines(c, a, sizeof(a), b, sizeof(b));
    ck(strstr(a, "U09 Test") != nullptr,
       "im Loeschmodus nennt die obere Zeile, was verschwindet");
}

/* ------------------------------------------------------------------------ */
static void test_first_free(void)
{
    group("Das Ziel steht auf dem ersten freien Platz");

    /* Frisch: nichts belegt. */
    j6_patchstore_init();
    {
        J6_Controller c(midi);
        c.useFirstFreeSlot();
        goToPage(c, "PATCH", "PATCH WRITE");
        char a[32]; c.paramAText(a, sizeof(a));
        note("frisches Geraet: %s", a);
        ck(strncmp(a, "U01", 3) == 0, "es beginnt bei U01");

        c.runPatchAction();
        setWriteTarget(c, 1); c.runPatchAction();
        setWriteTarget(c, 2); c.runPatchAction();
    }

    /* Nach einem Neustart mit drei belegten Plaetzen. */
    {
        J6_Controller c(midi);
        c.useFirstFreeSlot();
        goToPage(c, "PATCH", "PATCH WRITE");
        char a[32]; c.paramAText(a, sizeof(a));
        note("nach dem Neustart: %s", a);
        ck(strncmp(a, "U04", 3) == 0, "es springt auf den ersten freien danach");
        ck(strstr(a, "-free-") != nullptr, "und der ist wirklich frei");
    }
}

/* ------------------------------------------------------------------------ */
static void test_edited_marker(J6_Controller& c)
{
    group("Das Sternchen zeigt ungesicherte Aenderungen");

    selectPatch(c, 0);
    showScreen(c, "frisch geladen");
    ck(!c.isEdited(), "frisch geladen ist es aus");

    goToPage(c, "VCF", "VCF");
    c.onEncoder2(-3);
    showScreen(c, "Cutoff verstellt");
    ck(c.isEdited(), "ein verstellter Parameter setzt es");

    char h[24]; header(c, h, sizeof(h));
    ck(h[0] == '*', "und es steht vor dem Titel");

    c.onEncoder2(3);
    ck(!c.isEdited(), "Zurueckdrehen loescht es wieder");

    c.onEncoder2(-3);
    setWriteTarget(c, 20);
    ck(c.isEdited(), "vor dem Schreiben gesetzt");
    c.runPatchAction();
    showScreen(c, "nach dem Schreiben");
    ck(!c.isEdited(), "Speichern raeumt es weg");

    goToPage(c, "OUTPUT", "OUTPUT");
    c.onEncoder2(-4);
    ck(!c.isEdited(), "Master ist Geraeteeinstellung und setzt es nicht");

    goToPage(c, "VCF", "VCF");
    c.onEncoder2(-3);
    ck(c.isEdited(), "erst wieder gesetzt");
    selectPatch(c, 1);
    ck(!c.isEdited(), "und vom Laden eines anderen Patches geloescht");

    /* Die breiteste Kopfzeile, die entstehen kann. */
    goToPage(c, "VCF", "VCF"); c.onEncoder2(-3);
    goToPage(c, "PATCH", "PATCH WRITE");
    char n[12]; c.counterText(n, sizeof(n)); header(c, h, sizeof(h));
    note("\"%s\" + \"%s\" = %zu von 16", h, n, strlen(h) + strlen(n));
    ck(strlen(h) + strlen(n) <= 16, "Titel, Sternchen und Zaehler passen");

    goToPage(c, "PATCH", "PATCH");
    c.onEncoder2(JUNO_NPROGRAMS + J6_USER_PATCHES - 1 - c.listCursor());
    goToPage(c, "VCF", "VCF"); c.onEncoder2(-3);
    goToPage(c, "PATCH", "PATCH");
    c.counterText(n, sizeof(n)); header(c, h, sizeof(h));
    note("\"%s\" + \"%s\" = %zu von 16", h, n, strlen(h) + strlen(n));
    ck(strlen(h) + strlen(n) <= 16, "auch mit dem laengsten Zaehler");
}

/* ------------------------------------------------------------------------ */
static void test_every_page(J6_Controller& c)
{
    group("Jede Seite laesst sich anzeigen und bedienen");

    /* Der Menuedurchlauf, der die Kollision der Pseudo-Parameter mit dem
     * Arpeggiator gefunden hat: jede Seite jeder Sektion einmal aufgesucht,
     * beide Encoder gedreht, und alle Zeilen auf ihre Breite geprueft. */
    toTopLevel(c);
    const int sections = c.listCount();
    int pages = 0;

    for (int s = 0; s < sections; ++s) {
        toTopLevel(c);
        c.onEncoder1(s - c.listCursor());
        char name[32]; c.listEntry(c.listCursor(), name, sizeof(name));
        c.onSelectButton();

        char first[24]; snprintf(first, sizeof(first), "%s", c.title());
        for (int p = 0; p < 16; ++p) {
            char a[40], b[40];
            bodyLines(c, a, sizeof(a), b, sizeof(b));
            c.onEncoder2(1); c.onEncoder2(-1);
            c.onEncoder3(1); c.onEncoder3(-1);
            ++pages;
            c.onEncoder1(1);
            if (strcmp(c.title(), first) == 0) break;
        }
    }

    note("%d Sektionen, %d Seiten besucht", sections, pages);
    ck(pages >= sections, "jede Sektion hat mindestens eine Seite");
}

/* ------------------------------------------------------------------------ */
int main(void)
{
    printf("--- PicoFaceJ6, Bedienung und Speicher ---------------\n");

    j6_patchstore_init();
    {
        J6_Controller c(midi);

        test_list(c);
        test_write(c);
        test_roundtrip(c);
        test_erase(c);
        test_name_lineage(c);
        test_scrolling(c);
        test_naming(c);
        test_truncation(c);
        test_write_page(c);
        test_edited_marker(c);
        test_every_page(c);
    }

    test_first_free();

    printf("\n%s\n", g_failures ? "FEHLER VORHANDEN" : "alle Pruefungen bestanden");
    return g_failures ? 1 : 0;
}
