#include <iostream>
#include <string>
#include <stdlib.h>
#include <algorithm>
#include <iterator>
#include <map>
#include <list>
#include <sstream>
#include <Misc/SynthEngine.h>
#include <Misc/MiscFuncs.h>
#include <Misc/Bank.h>
#include <readline/readline.h>
#include <readline/history.h>

using namespace std;

extern map<SynthEngine *, MusicClient *> synthInstances;

MiscFuncs miscFuncs;

static int currentInstance = 0;

char welcomeBuffer [128];

string basics[] = {
    "setup",                      "show dynamic settings",
    "save",                       "save dynamic settings",
    "paths",                      "display bank root paths",
    "path add <s>",               "add bank root path",
    "path remove <n>",            "remove bank root path ID",
    "list root [n]",              "list banks in root ID or current",
    "list bank [n]",              "list instruments in bank ID or current",
    "list vector [n]",            "list settings for vector CHANNEL",
    "set reports [n]",            "set report destination (1 GUI console, other stderr)",
    "set root <n>",               "set current root path to ID",
    "set bank <n>",               "set current bank to ID",
    "set part [n1]",              "set part ID operations",
    "  program <n2>",             "loads instrument ID",
    "  channel <n2>",             "sets MIDI channel (> 15 disables)",
    "  destination <n2>",         "(1 main, 2 part, 3 both)",
    "set rootcc <n>",             "set CC for root path changes (> 119 disables)",
    "set bankcc <n>",             "set CC for bank changes (0, 32, other disables)",
    "set program <n>",            "set MIDI program change (0 off, other on)",
    "set activate <n>",           "set part activate (0 off, other on)",
    "set extend <n>",             "set CC for extended program change (> 119 disables)",
    "set available <n>",          "set available parts (16, 32, 64)",
    "set volume <n>",             "set master volume",
    "set shift <n>",              "set master key shift semitones (64 no shift)",
    "set alsa midi <s>",          "* set name of source",
    "set alsa audio <s>",         "* set name of hardware device",
    "set jack server <s>",        "* set server name",
    "set vector [n1]",            "set vector CHANNEL, operations",
    "  [x/y] cc <n2>",            "CC n2 is used for CHANNEL X or Y axis sweep",
    "  [x/y] features <n2>",      "sets CHANNEL X or Y features",
    "  [x/y] program <l/r> <n2>", "X or Y program change ID for CHANNEL L or R part",
    "  [x/y] control <n2> <n3>",  "sets n3 CC to use for X or Y feature n2 (2, 4, 8)",
    "  off",                      "disable vector for CHANNEL",
    "stop",                       "all sound off",
    "mode <s>",                   "change to different menus, (addsynth, subsynth, padsynth)",
    "exit",                       "tidy up and close Yoshimi",
    "? / help",                   "list commands",
    "end"
};

string subsynth [] = {
    "volume",                     "Not yet!",
    "pan",                        "Not yet!",
    "mode <s>",                   "change to different menus, (??, ???, ????) (.. / back) (top)",
    "end"
};

string errors [] = {
    "OK",
    "Value?",
    "Which Operation",
    " what?",
    "Out of range"
};


bool matchMove(char *&pnt, const char *word)
{
 bool found = miscFuncs.matchWord(pnt, word);
 if (found)
     pnt = miscFuncs.skipChars(pnt);
 return found;
}


bool cmdIfaceProcessCommand(char *buffer)
{
    map<SynthEngine *, MusicClient *>::iterator itSynth = synthInstances.begin();
    for(int i = 0; i < currentInstance; i++, ++itSynth);
    SynthEngine *synth = itSynth->first;
    Config &Runtime = synth->getRuntime();
    
    static int mode;
    string *commands = NULL;
    int error = 0;
    char *point = buffer;
    point = miscFuncs.skipSpace(point); // just to be sure
    if (matchMove(point, "stop"))
        synth->allStop();
    else if (matchMove(point, "setu"))
    {
        synth->SetSystemValue(109, 255);
        Runtime.Log("ALSA MIDI " + Runtime.alsaMidiDevice);
        Runtime.Log("ALSA AUDIO " + Runtime.alsaAudioDevice);
        Runtime.Log("Jack server " + Runtime.jackServer);
    }
    else if (matchMove(point, "path"))
    {
        if (matchMove(point, "add"))
        {
            int found = synth->getBankRef().addRootDir(point);
            if (!found)
            {
                Runtime.Log("Can't find path " + (string) point);
            }
            else
                Runtime.Log("Added new root ID " + miscFuncs.asString(found) + " as " + (string) point);
        }
        else if (matchMove(point, "remo"))
        {
            if (isdigit(point[0]))
            {
                int rootID = miscFuncs.string2int(point);
                string rootname = synth->getBankRef().getRootPath(rootID);
                if (rootname.empty())
                    Runtime.Log("Can't find path " + miscFuncs.asString(rootID));
                else
                {
                    synth->getBankRef().removeRoot(rootID);
                    Runtime.Log("Removed " + rootname);
                }
            }
            else
                error = 1;
        }
        else
            synth->SetSystemValue(110, 255);
    }
    else if (matchMove(point, "list"))
    {
        if (matchMove(point, "root"))
        {
            if (point[0] == 0)
                synth->SetSystemValue(111, 255);
            else
            {
                synth->SetSystemValue(111, miscFuncs.string2int(point));
            }
        }
        else if (matchMove(point, "bank"))
        {
            if (point[0] == 0)
                synth->SetSystemValue(112, 255);
            else
            {
                synth->SetSystemValue(112, miscFuncs.string2int(point));
            }
        }
        else if (matchMove(point, "vect"))
        {
            int chan;
            if (point[0] == 0)
                chan = Runtime.currentChannel;
            else
            {
                chan = miscFuncs.string2int(point);
                if (chan < NUM_MIDI_CHANNELS)
                    Runtime.currentChannel = chan;
                else
                    error = 4;
            }
            if (error != 4)
                synth->SetSystemValue(108, chan);
        }
        else
            error = 3;
    }
    
    else if (matchMove(point, "set"))
    {
        if (point[0] != 0)
            error = synth->commandSet(point);
        else
            error = 3;
    }
    
    else if (matchMove(point, "save"))
        synth->SetSystemValue(119, 255);
    else if (matchMove(point, "exit"))
    {
        Runtime.runSynth = false;
        return true;
    }
    else
    {
        int pushback = miscFuncs.matchWord(point, "..");
        if (pushback != 0 || matchMove(point, "mode"))
        {
            if (pushback != 0 || (mode > 0 && ( matchMove(point, "back") || matchMove(point, ".."))))
            {
                if (mode & 0x1f)
                    mode = 0;
                else
                    -- mode;
            }
            else if (matchMove(point, "top"))
                mode = 0;
            else if (matchMove(point, "add"))
                mode = 1;
            else if (matchMove(point, "sub"))
                mode = 0x21;
            else if (matchMove(point, "pad"))
                mode = 0x41;
            string extension;
            switch (mode)
            {
                case 0:
                    extension = " ";
                    break;
                case 1:
                    extension = "addsynth> ";
                    break;
                case 0x21:
                    extension = "subsynth> ";
                    break;
                case 0x41:
                    extension = "padsynth> ";
                    break;
            }
            sprintf(welcomeBuffer + 9, extension.c_str());
        }
        else
        {
            switch (mode)
            {
                case 0:
                    commands = basics;
                    break;
                case 1:
                    commands = subsynth;
                    break;
                case 0x21:
                    commands = subsynth;
                    break;
                case 0x41:
                    commands = subsynth;
                    break;
                }
            int word = 0;
            string left;
            string blanks;
            Runtime.Log("Commands");
            while (commands[word] != "end")
            {
                left = commands[word];
                if (left != "end")
                {
                    ++ word;
                    Runtime.Log("  " + left + blanks.assign<int>(30 - left.length(), ' ') + "- " + commands[word]);
                    ++ word;
                }
            }
            if (Runtime.consoleMenuItem)
                cout << basics[8] << endl;
                // stdout needs this if reports sent to console
            if (mode == 0)
                Runtime.Log("'*' entries need to be saved and Yoshimi restarted to activate");

        }
    }

    if (error == 3)
    {
        point[0] = 0; // sneaky :)
        Runtime.Log((string) buffer + errors[3]);
    }
    else if (error)
        Runtime.Log(errors[error]);
        
//    Runtime.Log("Mode " + miscFuncs.asString(mode));
    return false;
}


void cmdIfaceCommandLoop()
{
    int len = 512;
    char *cCmd = NULL;
    char buffer [len];
    memset(buffer, 0, len);
    bool exit = false;
    sprintf(welcomeBuffer, "\nyoshimi> ");
    while(!exit)
    {
        cCmd = readline(welcomeBuffer);
        if (cCmd)
        {
            memcpy(buffer, cCmd, len);
            string sCmd = cCmd;
            if(cCmd[0] != 0)
            {
                exit = cmdIfaceProcessCommand(buffer);
                add_history(cCmd);
                memset(buffer, 0, len + 1);
            }
            if(cCmd)
            {
                free(cCmd);
                cCmd = NULL;
            }
        }
    }
}
