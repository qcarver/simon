//
// C++ Interface: wav
//
// Description: 
//
//
// Author: Peter Grasch <bedahr@gmx.net>, (C) 2007
//
// Copyright: See COPYING file that comes with this distribution
//
//
#ifndef WAV_H
#define WAV_H

#include <QString>


/**
 *	@class WAV
 *	@brief The WAV Filewriter
 *
 *	This class provides all the needed utilities to write a standard compliant
 *	WAV File out of raw PCM data (such as the data provided by SoundControl,etc.).
 *
 *	@version 0.1
 *	@date 17.03.2007
 *	@author Peter Grasch
 *	
 *	@todo We use a single buffer that we resize every time; If it gets too large to handle we get "skippy" recordings
 */
class WAV{
private:
	char *waveData;  //!< here we store the audio data
	int length; //!< this is needed as there seems to be no way to determine the length of an array
	int samplerate; //!< the samplerate of the file
	int channels;
	QString filename; //!< Filename
	
	void writeHeader(QDataStream *dstream);
	void writeFormat(QDataStream *dstream);
	void writeDataChunk(QDataStream *dstream);
	void importDataFromFile(QString filename);
	int retrieveSampleRate();
	int retrieveChannels();
	
public:
    WAV(QString filename,int channels=0, int samplerate=0);

	char* getRawData(int& data);
    void addData(char* data, int length);
    bool writeFile(QString filename="");
    int getSampleRate() { return samplerate; }
    int getChannels() { return channels; }
    ~WAV();

};

#endif