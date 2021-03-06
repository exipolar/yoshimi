/*
    SUBnoteParameters.cpp - Parameters for SUBnote (SUBsynth)

    Original ZynAddSubFX author Nasca Octavian Paul
    Copyright (C) 2002-2005 Nasca Octavian Paul
    Copyright 2009, Alan Calvert

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

    This file is a derivative of a ZynAddSubFX original, modified February 2017
*/

#include "Params/SUBnoteParameters.h"

SUBnoteParameters::SUBnoteParameters(SynthEngine *_synth) : Presets(_synth)
{
    setpresettype("SUBnoteParameters");
    AmpEnvelope = new EnvelopeParams(64, 1, synth);
    AmpEnvelope->ADSRinit_dB(0, 40, 127, 25);
    FreqEnvelope = new EnvelopeParams(64, 0, synth);
    FreqEnvelope->ASRinit(30, 50, 64, 60);
    BandWidthEnvelope = new EnvelopeParams(64, 0, synth);
    BandWidthEnvelope->ASRinit_bw(100, 70, 64, 60);

    GlobalFilter = new FilterParams(2, 80, 40, 0, synth);
    GlobalFilterEnvelope = new EnvelopeParams(0, 1, synth);
    GlobalFilterEnvelope->ADSRinit_filter(64, 40, 64, 70, 60, 64);
    defaults();
}


void SUBnoteParameters::defaults(void)
{
    PVolume = 96;
    setPan(PPanning = 64);
    PAmpVelocityScaleFunction = 90;
    Pfixedfreq = 0;
    PfixedfreqET = 0;
    PBendAdjust = 88; // 64 + 24
    POffsetHz = 64;
    Pnumstages = 2;
    Pbandwidth = 40;
    Phmagtype = 0;
    Pbwscale = 64;
    Pstereo = true;
    Pstart = 1;

    PDetune = 8192;
    PCoarseDetune = 0;
    PDetuneType = 1;
    PFreqEnvelopeEnabled = 0;
    PBandWidthEnvelopeEnabled = 0;

    POvertoneSpread.type = 0;
    POvertoneSpread.par1 = 0;
    POvertoneSpread.par2 = 0;
    POvertoneSpread.par3 = 0;
    updateFrequencyMultipliers();

    for (int n = 0; n < MAX_SUB_HARMONICS; ++n)
    {
        Phmag[n] = 0;
        Phrelbw[n] = 64;
    }
    Phmag[0] = 127;

    PGlobalFilterEnabled = 0;
    PGlobalFilterVelocityScale = 64;
    PGlobalFilterVelocityScaleFunction = 64;

    AmpEnvelope->defaults();
    FreqEnvelope->defaults();
    BandWidthEnvelope->defaults();
    GlobalFilter->defaults();
    GlobalFilterEnvelope->defaults();
}


SUBnoteParameters::~SUBnoteParameters()
{
    delete AmpEnvelope;
    delete FreqEnvelope;
    delete BandWidthEnvelope;
    delete GlobalFilter;
    delete GlobalFilterEnvelope;
}


void SUBnoteParameters::setPan(char pan)
{
    PPanning = pan;
    if (!randomPan())
    {
        float t = (float)(PPanning - 1) / 126.0f;
        pangainL = cosf(t * HALFPI);
        pangainR = cosf((1.0f - t) * HALFPI);
    }
    else
        pangainL = pangainR = 0.7f;
}


void SUBnoteParameters::add2XML(XMLwrapper *xml)
{
    xml->information.SUBsynth_used = 1;

    xml->addpar("num_stages",Pnumstages);
    xml->addpar("harmonic_mag_type",Phmagtype);
    xml->addpar("start",Pstart);

    xml->beginbranch("HARMONICS");
    for (int i=0;i<MAX_SUB_HARMONICS;i++)
    {
        if ((Phmag[i]==0)&&(xml->minimal)) continue;
        xml->beginbranch("HARMONIC",i);
        xml->addpar("mag",Phmag[i]);
        xml->addpar("relbw",Phrelbw[i]);
        xml->endbranch();
    }
    xml->endbranch();

    xml->beginbranch("AMPLITUDE_PARAMETERS");
        xml->addparbool("stereo", (Pstereo) ? 1 : 0);
        xml->addpar("volume",PVolume);
        xml->addpar("panning",PPanning);
        xml->addpar("velocity_sensing",PAmpVelocityScaleFunction);
        xml->beginbranch("AMPLITUDE_ENVELOPE");
            AmpEnvelope->add2XML(xml);
        xml->endbranch();
    xml->endbranch();

    xml->beginbranch("FREQUENCY_PARAMETERS");
        xml->addparbool("fixed_freq",Pfixedfreq);
        xml->addpar("fixed_freq_et",PfixedfreqET);
        xml->addpar("bend_adjust", PBendAdjust);
        xml->addpar("offset_hz", POffsetHz);

        xml->addpar("detune",PDetune);
        xml->addpar("coarse_detune",PCoarseDetune);
        xml->addpar("overtone_spread_type", POvertoneSpread.type);
        xml->addpar("overtone_spread_par1", POvertoneSpread.par1);
        xml->addpar("overtone_spread_par2", POvertoneSpread.par2);
        xml->addpar("overtone_spread_par3", POvertoneSpread.par3);
        xml->addpar("detune_type",PDetuneType);

        xml->addpar("bandwidth",Pbandwidth);
        xml->addpar("bandwidth_scale",Pbwscale);

        xml->addparbool("freq_envelope_enabled",PFreqEnvelopeEnabled);
        if ((PFreqEnvelopeEnabled!=0)||(!xml->minimal))
        {
            xml->beginbranch("FREQUENCY_ENVELOPE");
            FreqEnvelope->add2XML(xml);
            xml->endbranch();
        }

        xml->addparbool("band_width_envelope_enabled",PBandWidthEnvelopeEnabled);
        if ((PBandWidthEnvelopeEnabled!=0)||(!xml->minimal))
        {
            xml->beginbranch("BANDWIDTH_ENVELOPE");
                BandWidthEnvelope->add2XML(xml);
            xml->endbranch();
        }
    xml->endbranch();

    xml->beginbranch("FILTER_PARAMETERS");
        xml->addparbool("enabled",PGlobalFilterEnabled);
        if ((PGlobalFilterEnabled!=0)||(!xml->minimal))
        {
            xml->beginbranch("FILTER");
                GlobalFilter->add2XML(xml);
            xml->endbranch();

            xml->addpar("filter_velocity_sensing",PGlobalFilterVelocityScaleFunction);
            xml->addpar("filter_velocity_sensing_amplitude",PGlobalFilterVelocityScale);

            xml->beginbranch("FILTER_ENVELOPE");
                GlobalFilterEnvelope->add2XML(xml);
            xml->endbranch();
        }
        xml->endbranch();
}


void SUBnoteParameters::updateFrequencyMultipliers(void)
{
    float par1 = POvertoneSpread.par1 / 255.0f;
    float par1pow = powf(10.0f,
            -(1.0f - POvertoneSpread.par1 / 255.0f) * 3.0f);
    float par2 = POvertoneSpread.par2 / 255.0f;
    float par3 = 1.0f - POvertoneSpread.par3 / 255.0f;
    float result;
    float tmp = 0.0f;
    int   thresh = 0;

    for(int n = 0; n < MAX_SUB_HARMONICS; ++n)
    {
        float n1     = n + 1.0f;
        switch(POvertoneSpread.type)
        {
            case 1:
                thresh = (int)(100.0f * par2 * par2) + 1;
                if (n1 < thresh)
                    result = n1;
                else
                    result = n1 + 8.0f * (n1 - thresh) * par1pow;
                break;

            case 2:
                thresh = (int)(100.0f * par2 * par2) + 1;
                if (n1 < thresh)
                    result = n1;
                else
                    result = n1 + 0.9f * (thresh - n1) * par1pow;
                break;

            case 3:
                tmp = par1pow * 100.0f + 1.0f;
                result = powf(n / tmp, 1.0f - 0.8f * par2) * tmp + 1.0f;
                break;

            case 4:
                result = n * (1.0f - par1pow) +
                    powf(0.1f * n, 3.0f * par2 + 1.0f) *
                    10.0f * par1pow + 1.0f;
                break;

            case 5:
                result = n1 + 2.0f * sinf(n * par2 * par2 * PI * 0.999f) *
                    sqrt(par1pow);
                break;

            case 6:
                tmp    = powf(2.0f * par2, 2.0f) + 0.1f;
                result = n * powf(par1 * powf(0.8f * n, tmp) + 1.0f, tmp) +
                    1.0f;
                break;

            case 7:
                result = (n1 + par1) / (par1 + 1);
                break;
            default:
                result = n1;
        }
        float iresult = floor(result + 0.5f);
        POvertoneFreqMult[n] = iresult + par3 * (result - iresult);
    }
}


void SUBnoteParameters::getfromXML(XMLwrapper *xml)
{
    Pnumstages=xml->getpar127("num_stages",Pnumstages);
    Phmagtype=xml->getpar127("harmonic_mag_type",Phmagtype);
    Pstart=xml->getpar127("start",Pstart);

    if (xml->enterbranch("HARMONICS"))
    {
        Phmag[0]=0;
        for (int i=0;i<MAX_SUB_HARMONICS;i++)
        {
            if (xml->enterbranch("HARMONIC",i)==0) continue;
            Phmag[i]=xml->getpar127("mag",Phmag[i]);
            Phrelbw[i]=xml->getpar127("relbw",Phrelbw[i]);
            xml->exitbranch();
        }
        xml->exitbranch();
    }

    if (xml->enterbranch("AMPLITUDE_PARAMETERS"))
    {
        int xpar = xml->getparbool("stereo", (Pstereo) ? 1 : 0);
        Pstereo = (xpar != 0) ? true : false;
        PVolume=xml->getpar127("volume",PVolume);
        setPan(xml->getpar127("panning",PPanning));
        PAmpVelocityScaleFunction=xml->getpar127("velocity_sensing",PAmpVelocityScaleFunction);
        if (xml->enterbranch("AMPLITUDE_ENVELOPE"))
        {
            AmpEnvelope->getfromXML(xml);
            xml->exitbranch();
        }
        xml->exitbranch();
    }

    if (xml->enterbranch("FREQUENCY_PARAMETERS"))
    {
        Pfixedfreq=xml->getparbool("fixed_freq",Pfixedfreq);
        PfixedfreqET=xml->getpar127("fixed_freq_et",PfixedfreqET);
        PBendAdjust  = xml->getpar127("bend_adjust", PBendAdjust);
        POffsetHz  = xml->getpar127("offset_hz", POffsetHz);

        PDetune=xml->getpar("detune",PDetune,0,16383);
        PCoarseDetune=xml->getpar("coarse_detune",PCoarseDetune,0,16383);
        POvertoneSpread.type =
            xml->getpar127("overtone_spread_type", POvertoneSpread.type);
        POvertoneSpread.par1 =
            xml->getpar("overtone_spread_par1", POvertoneSpread.par1, 0, 255);
        POvertoneSpread.par2 =
            xml->getpar("overtone_spread_par2", POvertoneSpread.par2, 0, 255);
        POvertoneSpread.par3 =
            xml->getpar("overtone_spread_par3", POvertoneSpread.par3, 0, 255);
        updateFrequencyMultipliers();
        PDetuneType=xml->getpar127("detune_type",PDetuneType);

        Pbandwidth=xml->getpar127("bandwidth",Pbandwidth);
        Pbwscale=xml->getpar127("bandwidth_scale",Pbwscale);

        PFreqEnvelopeEnabled=xml->getparbool("freq_envelope_enabled",PFreqEnvelopeEnabled);
        if (xml->enterbranch("FREQUENCY_ENVELOPE"))
        {
            FreqEnvelope->getfromXML(xml);
            xml->exitbranch();
        }

        PBandWidthEnvelopeEnabled=xml->getparbool("band_width_envelope_enabled",PBandWidthEnvelopeEnabled);
        if (xml->enterbranch("BANDWIDTH_ENVELOPE"))
        {
            BandWidthEnvelope->getfromXML(xml);
            xml->exitbranch();
        }
        xml->exitbranch();
    }

    if (xml->enterbranch("FILTER_PARAMETERS"))
    {
        PGlobalFilterEnabled=xml->getparbool("enabled",PGlobalFilterEnabled);
        if (xml->enterbranch("FILTER"))
        {
            GlobalFilter->getfromXML(xml);
            xml->exitbranch();
        }

        PGlobalFilterVelocityScaleFunction=xml->getpar127("filter_velocity_sensing",PGlobalFilterVelocityScaleFunction);
        PGlobalFilterVelocityScale=xml->getpar127("filter_velocity_sensing_amplitude",PGlobalFilterVelocityScale);

        if (xml->enterbranch("FILTER_ENVELOPE"))
        {
            GlobalFilterEnvelope->getfromXML(xml);
            xml->exitbranch();
        }
        xml->exitbranch();
    }
}


void SUBnoteParameters::getLimits(CommandBlock *getData)
{
    int control = getData->data.control;
    int insert = getData->data.insert;

    if (insert >= 6 && insert <= 7)
    { // do harmonics stuff
        if (insert == 7)
            getData->limits.def = 640;
        else if (control == 0)
            getData->limits.def = 1270;
        else
            getData->limits.def = 0;
        getData->data.type |= 0x40; // all learnable
        getData->limits.min = 0;
        getData->limits.max = 127;
        return;
    }

    // defaults
    int type = 0;
    int min = 0;
    int def = 0;
    int max = 127;

    switch (control)
    {
        case 0:
            type = 0x40;
            def = 960;
            break;

        case 1:
            type = 0x40;
            def = 900;
            break;

        case 2:
            type = 0x40;
            def = 640;
            break;

        case 16:
            type = 0x40;
            def = 400;
            break;

        case 17:
            type = 0x40;
            min = -64;
            max = 63;
            break;

            case 18:
                max = 1;
                break;

        case 32:
            type = 0x40;
            min = -8192;
            max = 8191;
            break;

        case 33:
            type = 0x40;
            break;

        case 34:
            max = 1;
            break;

        case 35:
            min = -8;
            max = 7;
            break;

        case 36:
            max = 3;
            break;

        case 37:
            min = -64;
            max = 63;
            break;

        case 40:
            max = 1;
            break;

        case 48:
        case 49:
        case 50:
            type = 0x40;
            max = 255;
            break;

        case 64:
            max = 1;
            break;

        case 80:
            min = 1;
            def = 1000;
            max = 5;
            break;

        case 81:
            max = 4;
            break;

        case 82:
            def = 1000;
            max = 2;
            break;

        case 96:
            max = 0;
            break;

        case 112:
            def = 10;
            max = 1;
            break;


        default:
            min = -1;
            def = -10;
            max = -1;
            break;

    }
    getData->data.type |= type;
    getData->limits.min = min;
    getData->limits.def = def;
    getData->limits.max = max;
}

void SUBnoteParameters::postrender(void)
{
    return;
}
