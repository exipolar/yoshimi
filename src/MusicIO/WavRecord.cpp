/*
    WavRecord.h

    Copyright 2009, Alan Calvert

    This file is part of yoshimi, which is free software: you can
    redistribute it and/or modify it under the terms of the GNU General
    Public License as published by the Free Software Foundation, either
    version 3 of the License, or (at your option) any later version.

    yoshimi is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with yoshimi.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio_ext.h>
#include <sys/stat.h>
#include <clocale>
#include <ctime>
#include <iostream>
#include <sndfile.h>

#include "Misc/Util.h"
#include "MusicIO/MusicIO.h"
#include "MusicIO/WavRecord.h"

WavRecord::WavRecord() :
    recordRunning(false),
    samplerate(0),
    buffersize(0),
    recordFifo(string()),
    toFifo(NULL),
    fromFifoSndfle(NULL),
    wavFile(string()),
    wavOutsnd(NULL),
    interleavedFloats(NULL),
    float32bit(true),
    recordState(nada),
    runRecordThread(false),
    pThread(0)
{
    setlocale( LC_TIME, "" ); // use compiler's native locale
}


WavRecord::~WavRecord()
{
    RecordStop();
    if (NULL != fromFifoSndfle)
        sf_close(fromFifoSndfle);
    if (NULL != wavOutsnd)
        sf_close(wavOutsnd);
    if (!recordFifo.empty() && isFifo(recordFifo))
        unlink(recordFifo.c_str());
}

bool WavRecord::PrepWav(void)
{
    samplerate = this->getSamplerate() ;
    buffersize = this->getBuffersize();
    int chk;
    char fifoname[] = { YOSHI_FIFO_DIR "/record.yoshimi.XXXXXX" };

    tferSamples = 512 / sizeof(float);
    tferBuf = new float[tferSamples];
    interleavedFloats = new float[sizeof(float) * buffersize * 2];
    if (NULL == tferBuf || NULL == interleavedFloats)
    {
        cerr << "Error, failed to allocate WavRecord buffers" << endl;
        goto bail_out;
    }
    memset(interleavedFloats, 0,  sizeof(float) * buffersize * 2);
    if (string(YOSHI_FIFO_DIR).empty() || !isDirectory(string(YOSHI_FIFO_DIR)))
    {
        cerr << "Error, invalid record fifo directory: "
             << string(YOSHI_FIFO_DIR) << endl;
        return false;
    }
    chk = mkstemp(fifoname);
    if (chk < 0)
    {
        cerr << "Error, failed to create fifoname " << endl;
        return false;
    }
    close(chk);
    unlink(fifoname);
    if ((chk = mkfifo(fifoname, S_IRUSR | S_IWRITE)))
    {
        cerr << "Error, failed to create fifo: " << recordFifo
             << " - " << strerror(chk) << endl;
        return false;
    }
    recordFifo = string(fifoname);

    pthread_attr_t attr;
    if ((chk = pthread_attr_init(&attr)))
    {
        cerr << "Error, failed to initialise record thread attributes: "
             << chk << endl;
        goto bail_out;
    }
    if ((chk = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)))
    {
        cerr << "Error, failed to set record thread detach state: " << chk << endl;
        goto bail_out;
    }

    runRecordThread = true;
    chk = pthread_create(&pThread, &attr, _recordThread, this);
    if (chk)
    {
        cerr << "Error, failed to start record thread: " << chk << endl;
        goto bail_out;
    }
    if (NULL != toFifo)
        fclose(toFifo);
    toFifo = fopen(recordFifo.c_str(), "w");
    if (NULL == toFifo)
    {
        cerr << "Error, failed to open toFifo: "
             << string(strerror(errno)) << endl;
        goto bail_out;
    }
    if (setvbuf(toFifo, NULL, _IOFBF, 1024 * 256))
    {
        cerr << "Error setting buffering on toFifo" << endl;
        goto bail_out;
    }
    return true;

bail_out:
    RecordClose();
    if (NULL != tferBuf)
        delete [] tferBuf;
    tferBuf = NULL;
    if (!recordFifo.empty() && isFifo(recordFifo))
        unlink(recordFifo.c_str());
    recordFifo.clear();
    return false;
}


void WavRecord::recordLog(string tag)
{
    static char stamp[12];
    if (Runtime.settings.verbose)
    {
        time_t ts = time(0);
        strftime(stamp, 12, "%H:%M:%S ", localtime(&ts));
        cout << stamp << tag << " " << wavFile << endl;
    }
}


void WavRecord::RecordStart(void)
{
    __fpurge(toFifo);
    recordRunning = true;
    recordLog("Record start");
}


void WavRecord::RecordStop(void)
{
    recordRunning = false;
    sf_command(wavOutsnd, SFC_UPDATE_HEADER_NOW, NULL, 0);
    sf_write_sync(wavOutsnd);
    __fpurge(toFifo);
    recordLog("Record stop");
}


void WavRecord::RecordClose(void)
{
    recordRunning = false;
    runRecordThread = false;
    if (pThread && pthread_cancel(pThread))
        cerr << "Error, failed to cancel record thread" << endl;
    if (NULL != interleavedFloats)
        delete [] interleavedFloats;
    interleavedFloats = NULL;
}


bool WavRecord::SetWavFile(string fpath, string& errmsg)
{
    int chk;
    string xfile = string(fpath);
    if (xfile.empty())
    {
        errmsg = "Empty file path";
        return false;
    }
    if (NULL != wavOutsnd)
    {
        if (wavFile == xfile)
            return true;
        else
        {
            sf_close(wavOutsnd);
            wavOutsnd = NULL;
        }
    }
    wavFile = xfile;
    if (isRegFile(wavFile))
    {
        // ah, an existing wav file
        wavOutsnd = sf_open(wavFile.c_str(), SFM_RDWR, &wavOutInfo) ;
        if (NULL == wavOutsnd)
        {
            errmsg = "Error opening " + wavFile + ": "
                     + string(sf_strerror(wavOutsnd));
            goto bail_out;
        }
        if (!((chk = wavOutInfo.format) & SF_FORMAT_WAV))
        {
            errmsg = wavFile + " is not a wav format file";
            goto bail_out;
        }
        chk &= SF_FORMAT_SUBMASK;
        if (!(chk == SF_FORMAT_FLOAT || chk == SF_FORMAT_PCM_16))
        {
            errmsg = wavFile + " is an incompatible wav format";
            goto bail_out;
        }
        if (wavOutInfo.samplerate != (int)samplerate || wavOutInfo.channels != 2)
        {
            errmsg = wavFile + " has incompatible samplerate/channels : "
                     + asString(wavOutInfo.samplerate) + "/"
                     + asString(wavOutInfo.channels);
            goto bail_out;
        }
        if (sf_seek(wavOutsnd, 0, SFM_WRITE | SEEK_END) < 0)
        {
            errmsg = "Error seeking to end of " + wavFile + ": "
                     + string(sf_strerror(wavOutsnd));
            goto bail_out;
        }
        recordLog("Open existing");
    }
    else
    {
        wavOutInfo.samplerate = samplerate;
        wavOutInfo.channels = 2;
        wavOutInfo.format = SF_FORMAT_WAV;
        if (Runtime.settings.Float32bitWavs)
        {
            wavOutInfo.format |= SF_FORMAT_FLOAT;
            float32bit = true;
        }
        else
        {
            wavOutInfo.format |= SF_FORMAT_PCM_16;
            float32bit = false;
        }
        wavOutsnd = sf_open(wavFile.c_str(), SFM_WRITE, &wavOutInfo);
        if (NULL == wavOutsnd)
        {
            errmsg = "Error opening new wav file " + wavFile
                     + " : " + string(sf_strerror(NULL));
            goto bail_out;
        }
        recordLog("Open new");
    }
    return true;

bail_out:
    wavFile.clear();
    if (NULL != wavOutsnd)
        sf_close(wavOutsnd);
    wavOutsnd = NULL;
    return false;
}


bool WavRecord::SetWavOverwrite(string& errmsg)
{
    if (NULL != wavOutsnd)
    {
        sf_close(wavOutsnd);
        unlink(wavFile.c_str());
    }
    wavOutsnd = sf_open(wavFile.c_str(), SFM_WRITE, &wavOutInfo);
    if (NULL != wavOutsnd)
    {
        recordLog("Overwrite");
        return true;
    }
    errmsg = "Error opening new wav file " + wavFile + " : " + string(sf_strerror(NULL));
    return false;
}


void WavRecord::feedRecord(float* samples_left, float *samples_right)
{
    int idx = 0;
    unsigned int buffersize = getBuffersize();
    for (unsigned int frame = 0; frame < buffersize; ++frame)
    {   // interleave floats
        interleavedFloats[idx++] = samples_left[frame];
        interleavedFloats[idx++] = samples_right[frame];
    }
    size_t wrote = fwrite_unlocked(interleavedFloats, sizeof(float) * 2,
                                   buffersize, toFifo);
    if (wrote != buffersize)
        cerr << "Error,  short write in feedRecord, " << wrote << " / "
             << buffersize << endl;
}


void WavRecord::_wavCleanup(void *arg)
{
    static_cast<WavRecord*>(arg)->wavCleanup();
}


void WavRecord::wavCleanup(void)
{
    recordRunning = false;
    if (NULL != toFifo)
    {
        if (fclose(toFifo))
            cerr << "Error closing fifo feed: " << string(strerror(errno)) << endl;
        toFifo = NULL;
    }
    if (NULL != fromFifoSndfle)
    {
        if (sf_close(fromFifoSndfle))
            cerr << "Error closing fifo read sndfle: "
                 << string(sf_strerror(fromFifoSndfle)) << endl;
        fromFifoSndfle = NULL;
    }
    if (NULL != wavOutsnd)
    {
        if (sf_close(wavOutsnd))
            cerr << "Error closing wav file " << wavFile
                 << string(sf_strerror(wavOutsnd)) << endl;
        else
            recordLog("Close");
        wavOutsnd = NULL;
    }
    if (!recordFifo.empty() && isFifo(recordFifo))
        unlink(recordFifo.c_str());
    recordFifo.clear();
}


void *WavRecord::_recordThread(void *arg)
{
    return static_cast<WavRecord*>(arg)->recordThread();
}


void *WavRecord::recordThread(void)
{
    if (NULL == tferBuf)
    {
        cerr << "Error, record thread transfer buffer unavailable" << endl;
        pthread_exit(NULL);
    }

    fromFifoInfo.samplerate = samplerate;
    fromFifoInfo.channels = 2;
    fromFifoInfo.format = SF_FORMAT_RAW | SF_FORMAT_FLOAT;
    fromFifoSndfle = sf_open(recordFifo.c_str(), SFM_READ, &fromFifoInfo);
    if (NULL == fromFifoSndfle)
    {
        cerr << "Error opening fifo for input: "
             << string(sf_strerror(fromFifoSndfle)) << endl;
        pthread_exit(NULL);
    }
    sf_count_t samplesRead;
    sf_count_t wroteSamples;
    pthread_cleanup_push(_wavCleanup, this);
    while (runRecordThread == true)
    {
        pthread_testcancel();
        samplesRead = sf_read_float(fromFifoSndfle, tferBuf, tferSamples);
        if (recordRunning)
        {
            if (tferSamples != samplesRead)
            {
                cerr << "Error, dodgy read from recordFifo, read "
                     << samplesRead << " of " << tferSamples << " frames" << endl;
            }
            if (samplesRead > 0)
            {
                wroteSamples = sf_write_float(wavOutsnd, tferBuf, samplesRead);
                if (wroteSamples != samplesRead)
                    cerr << "Error, dodgy write to wav file"
                         << wroteSamples << " / " << samplesRead << " frames" << endl;
            }
        }
    }
    pthread_cleanup_pop(1);
    return NULL;
}
