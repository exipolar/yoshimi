/*
    MidiLearn.cpp

    Copyright 2016-2017 Will Godfrey

    This file is part of yoshimi, which is free software: you can redistribute
    it and/or modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either version 2 of
    the License, or (at your option) any later version.

    yoshimi is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.   See the GNU General Public License (version 2 or
    later) for more details.

    You should have received a copy of the GNU General Public License along with
    yoshimi; if not, write to the Free Software Foundation, Inc., 51 Franklin
    Street, Fifth Floor, Boston, MA  02110-1301, USA.

    Modified march 2017
*/

#include <iostream>
#include <bitset>
#include <unistd.h>
#include <list>
#include <string>
#include <unistd.h>

using namespace std;

#include "Interface/MidiLearn.h"
#include "Interface/InterChange.h"
#include "Misc/MiscFuncs.h"
#include "Misc/XMLwrapper.h"
#include "Misc/SynthEngine.h"

MidiLearn::MidiLearn(SynthEngine *_synth) :
    learning(false),
    synth(_synth)
{
 //init

}


MidiLearn::~MidiLearn()
{
    //close
}


void MidiLearn::setTransferBlock(CommandBlock *getData, string name)
{
    learnTransferBlock = *getData;
    learnedName = name;
    if (getData->data.type & 8)
        return; // don't spam ourselves!
    learning = true;
    synth->getRuntime().Log("Learning");
    updateGui(21);
}


bool MidiLearn::runMidiLearn(float _value, unsigned int CC, unsigned char chan, unsigned char category)
{
    if (learning)
    {
        insert(CC, chan);
        return true; // block while learning
    }

    int lastpos = -1;
    LearnBlock foundEntry;
    bool firstLine = true;
    while (lastpos != -2)
    {
        lastpos = findEntry(midi_list, lastpos, CC, chan, &foundEntry, false);
        if (lastpos == -3)
            return false;
        float value = _value; // needs to be refetched each loop
        if (category & 2)
        {
            value = value / 128.0; // convert from 14 bit
        }
        int status = foundEntry.status;
        if ((status & 4) == 4) // it's muted
            continue;

        int minIn = foundEntry.min_in;
        int maxIn = foundEntry.max_in;
        if (minIn > maxIn)
        {
            value = 127 - value;
            swap(minIn, maxIn);
        }

        if (status & 2) // limit
        {
            if (value < minIn)
                value = minIn;
            else if (value > maxIn)
                value = maxIn;
        }
        else // compress
        {
            int range = maxIn - minIn;
            value = (value * range / 127) + minIn;
        }

        int minOut = foundEntry.min_out;
        int maxOut = foundEntry.max_out;
        if (maxOut - minOut != 127) // its a range change
        {
            value = value / 127;
            value = minOut +((maxOut - minOut) * value);
        }
        else if (minOut != 0) // it's just a shift
        {
            value += minOut;
        }

        CommandBlock putData;
        putData.data.value = value;
        putData.data.type = 0x48 | (foundEntry.data.type & 0x80);
        // write command from midi with original integer / float type
        putData.data.control = foundEntry.data.control;
        putData.data.part = foundEntry.data.part;
        putData.data.kit = foundEntry.data.kit;
        putData.data.engine = foundEntry.data.engine;
        putData.data.insert = foundEntry.data.insert;
        putData.data.parameter = foundEntry.data.parameter;
        putData.data.par2 = foundEntry.data.par2;
        unsigned int putSize = sizeof(putData);
        if (writeMidi(&putData, putSize, category & 1))
        {
            if (firstLine && !(category & 1)) // not in_place
            // we only want to send an activity once
            // and it's not relevant to jack freewheeling
            {
                if (CC > 0xff)
                    putData.data.type |= 0x10; // mark as NRPN
                firstLine = false;
                putData.data.control = 24;
                putData.data.part = 0xd8;
                putData.data.kit = (CC & 0xff);
                putData.data.engine = chan;
                writeMidi(&putData, putSize, category & 1);
            }
        }
        if (lastpos == -1) // blocking all of this CC/chan pair
            return true;
    }
    return false;
}


bool MidiLearn::writeMidi(CommandBlock *putData, unsigned int writesize, bool in_place)
{
    char *point = (char*) putData;
    unsigned int towrite = writesize;
    unsigned int wrote = 0;
    unsigned int found;
    unsigned int tries = 0;
    bool ok = true;
    if (in_place)
    {
        synth->interchange.commandSend(putData);
        synth->interchange.returns(putData);
    }
    else
    {
        if (jack_ringbuffer_write_space(synth->interchange.fromMIDI) >= writesize)
        {
            while (towrite && tries < 3)
            {
                found = jack_ringbuffer_write(synth->interchange.fromMIDI, point, towrite);
                wrote += found;
                point += found;
                towrite -= found;
                ++tries;
            }
            if (towrite)
            {
                ok = false;
                    synth->getRuntime().Log("Unable to write data to fromMidi buffer", 2);
            }
        }
        else
        {
            synth->getRuntime().Log("fromMidi buffer full!", 2);
            ok = false;
        }
    }
    return ok;
}


/*
 * This will only be called by incoming midi. It is the only function that
 * needs to be really quick
 */
int MidiLearn::findEntry(list<LearnBlock> &midi_list, int lastpos, unsigned int CC, unsigned char chan, LearnBlock *block, bool show)
{
    int newpos = 0; // 'last' comes in at -1 for the first call
    list<LearnBlock>::iterator it = midi_list.begin();

    while (newpos <= lastpos && it != midi_list.end())
    {
        ++ it;
        ++ newpos;
    }
    if (it == midi_list.end())
        return -3;

    while ((CC != it->CC || (it->chan != 16 && chan != it->chan)) &&  it != midi_list.end())
    {
        ++ it;
        ++ newpos;
    }
    if (it == midi_list.end())
        return -3;

    while (CC == it->CC && it != midi_list.end())
    {
        if ((it->chan >= 16 || chan == it->chan) && CC == it->CC)
        {
            if (show)
                synth->getRuntime().Log("Found line " + it->name + "  at " + to_string(newpos)); // a test!
            block->chan = it->chan;
            block->CC = it->CC;
            block->min_in = it->min_in;
            block->max_in = it->max_in;
            block->status = it->status;
            block->min_out = it->min_out;
            block->max_out = it->max_out;
            block->data = it->data;
            if ((it->status & 5) == 1)
                return -1; // don't allow any more of this CC and channel;
            return newpos;
        }
        ++ it;
        ++ newpos;
    }
    return -2;
}


void MidiLearn::listLine(int lineNo)
{
    list<LearnBlock>::iterator it = midi_list.begin();
    int found = 0;
    if (midi_list.size() == 0)
    {
        synth->getRuntime().Log("No learned lines");
        return;
    }

    while (it != midi_list.end() && found < lineNo)
    {
        ++ it;
        ++ found;
    }
    if (it == midi_list.end())
    {
        synth->getRuntime().Log("No entry for number " + to_string(lineNo + 1));
        return;
    }
    else
    {
        int status = it->status;
        string mute = "";
        if (status & 4)
            mute = "  muted";
        string limit = "";
        if (status & 2)
            limit = "  limiting";
        string block = "";
        if (status & 1)
            block = "  blocking";
        synth->getRuntime().Log("Line " + to_string(lineNo) + mute
                + "  CC " + to_string((int)it->CC)
                + "  Chan " + to_string((int)it->chan)
                + "  Min " + to_string((int)it->min_in)
                + "  Max " + to_string((int)it->max_in)
                + limit + block + "  " + it->name);
    }
}


void MidiLearn::listAll(list<string>& msg_buf)
{
    list<LearnBlock>::iterator it = midi_list.begin();
    int lineNo = 0;
    if (midi_list.size() == 0)
    {
        msg_buf.push_back("No learned lines");
        return;
    }
    string CCtype;
    int CC;
    msg_buf.push_back("Midi learned:");
    while (it != midi_list.end())
    {
        CC = it->CC;
        if (CC < 0xff)
            CCtype = to_string(CC);
        else
            CCtype = asHexString((CC >> 8) & 0x7f) + asHexString(CC & 0x7f) + " h";
        msg_buf.push_back("Line " + to_string(lineNo + 1) + "  CC " + CCtype + "  Chan " + to_string((int)it->chan) + "  " + it->name);
        ++ it;
        ++ lineNo;
    }
}


bool MidiLearn::remove(int itemNumber)
{
    list<LearnBlock>::iterator it = midi_list.begin();
    int found = 0;
    while (found < itemNumber && it != midi_list.end())
    {
        ++ found;
        ++ it;
    }
    if (it != midi_list.end())
    {
        midi_list.erase(it);
        return true;
    }
    return false;
}


void MidiLearn::generalOpps(int value, unsigned char type, unsigned char control, unsigned char part, unsigned char _kit, unsigned char engine, unsigned char insert, unsigned char parameter, unsigned char par2)
{
    unsigned int kit = _kit; // may need to set as an NRPN
    if (control == 96)
    {
        midi_list.clear();
        updateGui();
        synth->getRuntime().Log("List cleared");
        return;
    }

    string name;
    if (control == 241)
    {
        name = (miscMsgPop(par2));
        if (loadList(name))
            synth->getRuntime().Log("Loaded " + name);
        updateGui();
        return;
    }
    if (control == 242) // list controls
    {
        int tmp = synth->SetSystemValue(106, par2);
        if (tmp == -1)
        {
            synth->getRuntime().Log("No entry for number " + to_string(int(par2)));
        }
        else
        {
            name = miscMsgPop(tmp);
            if (loadList(name))
                synth->getRuntime().Log("Loaded " + name);
            updateGui();
        }
        return;
    }
    if (control == 245)
    {
        name = (miscMsgPop(par2));
        if (saveList(name))
            synth->getRuntime().Log("Saved " + name);
        return;
    }
    if (control == 255)
    {
        learning = false;
        synth->getRuntime().Log("Midi Learn cancelled");
        updateGui(control);
        return;
    }

    // line controls
    LearnBlock entry;
    int lineNo = 0;
    list<LearnBlock>::iterator it = midi_list.begin();
    it = midi_list.begin();

    while(lineNo < value && it != midi_list.end())
    {
        ++ it;
        ++lineNo;
    }

    if (it == midi_list.end())
    {
        synth->getRuntime().Log("Line " + to_string(int(value)) + " not found");
        return;
    }

    if (insert == 0xff) // don't change
        insert = it->min_in;

    if (parameter == 0xff)
        parameter = it->max_in;

    if (type == 0xff)
        type = it->status;

    if (kit == 255 || it->CC > 0xff) // might be an NRPN
        kit = it->CC;

    if (engine == 255)
        engine = it->chan;

    if (control == 16)
    {
        bool moveLine = true;
        list<LearnBlock>::iterator nextit = it;
        list<LearnBlock>::iterator lastit = it;
        ++ nextit;
        -- lastit;
        if (it == midi_list.begin() && nextit->CC >= kit)
        {
            if (nextit->CC > kit || nextit->chan >= engine)
                moveLine = false;
        }
        else if (nextit == midi_list.end() && lastit->CC <= kit)
        {
            if (lastit->CC < kit || lastit->chan <= engine)
                moveLine = false;
        }

        // here be dragons :(
        else if (kit > it->CC)
        {
            if (nextit->CC > kit)
                moveLine = false;
        }
        else if (kit < it->CC)
        {
            if (lastit->CC < kit)
                moveLine = false;
        }
        else if (engine > it->chan)
        {
            if (nextit->CC > kit || nextit->chan >= engine)
                moveLine = false;
        }
        else if (engine < it->chan)
        {
            if (lastit->CC < kit || lastit->chan <= engine)
                moveLine = false;
        }

        if (!moveLine)
            control = 7; // change this as we're not moving the line
    }

    if (control == 8)
    {
        remove(value);
        updateGui();
        synth->getRuntime().Log("Removed line " + to_string(int(value)));
        return;
    }

    if (control < 8)
    {
        CommandBlock putData;
        memset(&putData.bytes, 255, sizeof(putData));
        // need to work on this lot
        putData.data.value = value;
        putData.data.type = type;
        putData.data.control = 7;
        putData.data.kit = kit;
        putData.data.engine = engine;
        putData.data.insert = insert;
        putData.data.parameter = parameter;
        it->CC = kit;
        it->chan = engine;
        it->min_in = insert;
        it->max_in = parameter;
        it->status = type;
        writeToGui(&putData);
        return;
    }

    if (control == 16)
    {
        entry.CC = kit;
        entry.chan = engine;
        entry.min_in = insert;
        entry.max_in = parameter;
        entry.status = type;
        entry.min_out = it->min_out;
        entry.max_out = it->max_out;
        entry.name = it->name;
        entry.data = it->data;
        unsigned int CC = entry.CC;
        int chan = entry.chan;

        midi_list.erase(it);

        it = midi_list.begin();
        int lineNo = 0;
        if (midi_list.size() > 0)
        { // CC is priority
            while(CC > it->CC && it != midi_list.end())
            { // find start of group
                ++it;
                ++lineNo;
            }
            while(CC == it->CC && chan >= it->chan && it != midi_list.end())
            { // insert at end of same channel
                ++it;
                ++lineNo;
            }
        }

        if (it == midi_list.end())
            midi_list.push_back(entry);
        else
            midi_list.insert(it, entry);
        updateGui();
        return;
    }
    // there may be more later!
}


void MidiLearn::insert(unsigned int CC, unsigned char chan)
{
    /*
     * This will eventually be part of a paging system of
     * 128 lines for the Gui.
     */
    if (midi_list.size() >= MIDI_LEARN_BLOCK)
    {
        GuiThreadMsg::sendMessage(synth,GuiThreadMsg::GuiAlert,miscMsgPush("Midi Learn full!"));
        synth->getRuntime().Log("Midi Learn full!");
        learning = false;
        return;
    }
    list<LearnBlock>::iterator it;
    LearnBlock entry;
    unsigned char stat = 0;
    if (CC > 0xff)
        stat |= 1; // mark as NRPN
     /*
      * this has to be first as the transfer block will be corrupted
      * when we call for the limits of this control. Should be a better
      * way to do this!
      */
    entry.data.type = learnTransferBlock.data.type & 0x80;
    entry.data.control = learnTransferBlock.data.control;
    entry.data.part = learnTransferBlock.data.part;
    entry.data.kit = learnTransferBlock.data.kit;
    entry.data.engine = learnTransferBlock.data.engine;
    entry.data.insert = learnTransferBlock.data.insert;
    entry.data.parameter = learnTransferBlock.data.parameter;
    entry.data.par2 = learnTransferBlock.data.par2;

    synth->interchange.returnLimits(&learnTransferBlock);
    entry.chan = chan;
    entry.CC = CC;
    entry.min_in = 0;
    entry.max_in = 127;
    entry.status = stat;
    entry.min_out = learnTransferBlock.limits.min;
    entry.max_out = learnTransferBlock.limits.max;
    entry.name = learnedName;


    it = midi_list.begin();
    int lineNo = 0;
    if (midi_list.size() > 0)
    { // CC is priority
        while(CC > it->CC && it != midi_list.end()) // CC is priority
        { // find start of group
            ++it;
            ++lineNo;
        }
        while(CC == it->CC && chan >= it->chan && it != midi_list.end())
        { // insert at end of same channel
            ++it;
            ++lineNo;
        }
    }
    if (it == midi_list.end())
        midi_list.push_back(entry);
    else
        midi_list.insert(it, entry);

    synth->getRuntime().Log("Learned ");
    unsigned int CCh = entry.CC;
    string CCtype;
    if (CCh < 0xff)
        CCtype = to_string(CCh);
    else
        CCtype = asHexString((CCh >> 8) & 0x7f) + asHexString(CCh & 0x7f) + " h";
    synth->getRuntime().Log("CC " + CCtype + "  Chan " + to_string((int)entry.chan) + "  " + entry.name);
    updateGui(1);
    learning = false;
}


void MidiLearn::writeToGui(CommandBlock *putData)
{
    if (!synth->getRuntime().showGui)
        return;
    putData->data.part = 0xd8;
    unsigned int writesize = sizeof(*putData);
    char *point = (char*)putData;
    unsigned int towrite = writesize;
    unsigned int wrote = 0;
    unsigned int found = 0;
    unsigned int tries = 0;
    if (jack_ringbuffer_write_space(synth->interchange.toGUI) >= writesize)
    {
        while (towrite && tries < 3)
        {
            found = jack_ringbuffer_write(synth->interchange.toGUI, point, towrite);
            wrote += found;
            point += found;
            towrite -= found;
            ++tries;
        }
        if (towrite)
            synth->getRuntime().Log("Unable to write data to toGui buffer", 2);
    }
    else
        synth->getRuntime().Log("toGui buffer full!", 2);
}


void MidiLearn::updateGui(int opp)
{
    if (!synth->getRuntime().showGui)
        return;
    CommandBlock putData;
    if (opp == 21)
    {
        putData.data.control = 21;
        putData.data.par2 = miscMsgPush("Learning " + learnedName);
    }
    else if (opp == 255)
    {
        putData.data.control = 255;
        putData.data.par2 = 0xff;
    }
    else
    {
        putData.data.control = 96;
        putData.data.par2 = 0xff;
    }
    putData.data.value = 0;
    writeToGui(&putData);

    if (opp > 1) // sending back message gui
        return;

    int lineNo = 0;
    list<LearnBlock>::iterator it;
    it = midi_list.begin();
    while (it != midi_list.end())
    {
        unsigned int newCC = it->CC;
        putData.data.value = lineNo;
        putData.data.type = it->status;
        putData.data.control = 16;
        putData.data.kit = (newCC & 0xff);
        putData.data.engine = it->chan;
        putData.data.insert = it->min_in;
        putData.data.parameter = it->max_in;
        putData.data.par2 = miscMsgPush(it->name);
        writeToGui(&putData);
        if (newCC > 0xff)
        {
            putData.data.control = 9; // it's an NRPN
            putData.data.engine = ((newCC >> 8) & 0x7f);
            writeToGui(&putData);
        }
        ++it;
        ++lineNo;
    }
    if (opp == 1 && synth->getRuntime().showLearnedCC == true) // open the gui editing window
    {
        putData.data.control = 22;
        writeToGui(&putData);
    }
}


bool MidiLearn::saveList(string name)
{
    if (name.empty())
    {
        synth->getRuntime().Log("No filename");
        return false;
    }

    if (midi_list.size() == 0)
    {
        synth->getRuntime().Log("No Midi Learn list");
        return false;
    }

    string file = setExtension(name, "xly");
    legit_pathname(file);

    synth->getRuntime().xmlType = XML_MIDILEARN;
    XMLwrapper *xml = new XMLwrapper(synth);
    if (!xml)
    {
        synth->getRuntime().Log("Save Midi Learn failed xmltree allocation");
        return false;
    }
    bool ok = true;
    int ID = 0;
    list<LearnBlock>::iterator it;
    it = midi_list.begin();
    xml->beginbranch("MIDILEARN");
        while (it != midi_list.end())
        {
            xml->beginbranch("LINE", ID);
            xml->addparbool("Mute", (it->status & 4) > 0);
                xml->addpar("Midi_Controller", it->CC);
                xml->addpar("Midi_Channel", it->chan);
                xml->addpar("Midi_Min", it->min_in);
                xml->addpar("Midi_Max", it->max_in);
                xml->addparbool("Limit", (it->status & 2) > 0);
                xml->addparbool("Block", (it->status & 1) > 0);
                xml->addpar("Convert_Min", it->min_out);
                xml->addpar("Convert_Max", it->max_out);
                xml->beginbranch("COMMAND");
                    xml->addpar("Type", it->data.type);
                    xml->addpar("Control", it->data.control);
                    xml->addpar("Part", it->data.part);
                    xml->addpar("Kit_Item", it->data.kit);
                    xml->addpar("Engine", it->data.engine);
                    xml->addpar("Insert", it->data.insert);
                    xml->addpar("Parameter", it->data.parameter);
                    xml->addpar("Secondary_Parameter", it->data.par2);
                    xml->addparstr("Command_Name", it->name.c_str());
                    xml->endbranch();
            xml->endbranch();
            ++it;
            ++ID;
        }
        xml->endbranch();

    if (xml->saveXMLfile(file))
        synth->addHistory(file, 6);
    else
    {
        synth->getRuntime().Log("Failed to save data to " + file);
        ok = false;
    }
    delete xml;
    return ok;
}


bool MidiLearn::loadList(string name)
{
    if (name.empty())
    {
        synth->getRuntime().Log("No filename");
        return false;
    }
    string file = setExtension(name, "xly");
    legit_pathname(file);
    if (!isRegFile(file))
    {
        synth->getRuntime().Log("Can't find " + file);
        return false;
    }
    XMLwrapper *xml = new XMLwrapper(synth);
    if (!xml)
    {
        synth->getRuntime().Log("Load Midi Learn failed XMLwrapper allocation");
        return false;
    }
    xml->loadXMLfile(file);

    if (!xml->enterbranch("MIDILEARN"))
    {
        synth->getRuntime().Log("Extract Data, no MIDILEARN branch");
        return false;
    }
    LearnBlock entry;
    CommandBlock real;
    midi_list.clear();
    int ID = 0;
    int status;
    while (true)
    {
        status = 0;
        if (!xml->enterbranch("LINE", ID))
            break;
        else
        {

            if (xml->getparbool("Mute",0))
                status |= 4;
            entry.CC = xml->getpar("Midi_Controller", 0, 0, 0xfffff);
            entry.chan = xml->getpar127("Midi_Channel", 0);
            entry.min_in = xml->getpar127("Midi_Min", 0);
            entry.max_in = xml->getpar127("Midi_Max", 127);
            if (xml->getparbool("Limit",0))
                status |= 2;
            if (xml->getparbool("Block",0))
                status |= 1;
            entry.min_out = xml->getpar("Convert_Min", 0, -16384, 16383);
            entry.max_out = xml->getpar("Convert_Max", 0, -16384, 16383);
            xml->enterbranch("COMMAND");
                entry.data.type = xml->getpar255("Type", 0); // ??
                real.data.control = entry.data.control = xml->getpar255("Control", 0);
                real.data.part = entry.data.part = xml->getpar255("Part", 0);
                real.data.kit = entry.data.kit = xml->getpar255("Kit_Item", 0);
                real.data.engine = entry.data.engine = xml->getpar255("Engine", 0);
                real.data.insert = entry.data.insert = xml->getpar255("Insert", 0);
                real.data.parameter = entry.data.parameter = xml->getpar255("Parameter", 0);
                real.data.par2 = entry.data.par2 = xml->getpar255("Secondary_Parameter", 0);
                xml->exitbranch();
            xml->exitbranch();
            entry.status = status;
            real.data.value = 0;
            real.data.type = 0x1b;
            /*
             * Need to modify resolveReplies so that we can
             * get just the name without this loopback.
             */
            synth->interchange.resolveReplies(&real);
            entry.name = learnedName;
            midi_list.push_back(entry);
            ++ ID;
        }
    }

    xml->endbranch(); // MIDILEARN
    synth->addHistory(file, 6);
    delete xml;
    return true;
}
