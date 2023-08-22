#include "AudioFile.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "soundtouch/SoundTouch.h"

DEFINE_string(example_file_path, "input.wav", "Test wave input file");
DEFINE_string(output_file_path, "output.wav", "Test wave output file");
DEFINE_int32(max_samples, 16777216, "Max samples per channel");
DEFINE_int32(tempo, -5, "Tempo change");

using namespace soundtouch;

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // read a file to memory to mimic server behaviour
    std::string filePath = FLAGS_example_file_path;
    std::ifstream file (filePath, std::ios::binary);

    if (! file.good())
    {
        LOG(ERROR) << "ERROR: File doesn't exist or otherwise can't load file\t" << filePath;
        return EXIT_FAILURE;
    }
    
    std::vector<uint8_t> fileData;

	file.unsetf (std::ios::skipws);

	file.seekg (0, std::ios::end);
	size_t length = file.tellg();
	file.seekg (0, std::ios::beg);

	fileData.resize (length);

	file.read(reinterpret_cast<char*> (fileData.data()), length);
	file.close();

	if (file.gcount() != length)
	{
		LOG(ERROR) << "ERROR: Couldn't read entire file\t" << filePath;
		return EXIT_FAILURE;
	}

    if (fileData.size() < 12)
    {
        LOG(ERROR) << "ERROR: File is not a valid audio file\t" << filePath;
        return EXIT_FAILURE;
    }

    // load audio
    AudioFile<float> audioFile;
    bool status = audioFile.loadFromMemory(fileData);
    if (!status) {
        LOG(ERROR) << "ERROR: File load error\t" << filePath;
        return EXIT_FAILURE;
    }
    audioFile.printSummary();

    // test sound stretch
    LOG(INFO) << SoundTouch::getVersionString();
    SoundTouch soundTouch;

    int nSampleRate = audioFile.getSampleRate();
    int nChannels = audioFile.getNumChannels();
    soundTouch.setSampleRate(nSampleRate);
    soundTouch.setChannels(nChannels);

    // change tempo
    soundTouch.setTempoChange((float)FLAGS_tempo);

    // set for speech
    soundTouch.setSetting(SETTING_SEQUENCE_MS, 40);
    soundTouch.setSetting(SETTING_SEEKWINDOW_MS, 15);
    soundTouch.setSetting(SETTING_OVERLAP_MS, 8);

    // fill
    int nSamples = audioFile.getNumSamplesPerChannel();
    if (nSamples > FLAGS_max_samples) {
        nSamples = FLAGS_max_samples;
    }
    int nSamplesRead = nSamples * nChannels;
    SAMPLETYPE sampleBuffer[nSamplesRead];
    for (int i = 0; i < nSamples; i++)
    {
        for (int channel = 0; channel < nChannels; channel++)
        {
            sampleBuffer[i * nChannels + channel] = audioFile.samples[channel][i];
        }
    }
    soundTouch.putSamples(sampleBuffer, nSamples);

    // stretch
    int targetSamples = nSamples * 100 / (100 + FLAGS_tempo);
    audioFile.setAudioBufferSize(nChannels, targetSamples);
    int offset = 0;
    int buffNumSamples;
    do
    {
        buffNumSamples = soundTouch.receiveSamples(sampleBuffer, nSamples);
        for (int i = 0; i < buffNumSamples; i++)
        {
            for (int channel = 0; channel < nChannels; channel++)
            {
                audioFile.samples[channel][i + offset] = sampleBuffer[i * nChannels + channel];
            }
        }
        offset += buffNumSamples * nChannels;
        LOG(INFO) << "Processed: " << offset;
    } while (buffNumSamples != 0);

    soundTouch.flush();
    do
    {
        buffNumSamples = soundTouch.receiveSamples(sampleBuffer, nSamples);
        for (int i = 0; i < buffNumSamples; i++)
        {
            for (int channel = 0; channel < nChannels; channel++)
            {
                audioFile.samples[channel][i + offset] = sampleBuffer[i * nChannels + channel];
            }
        }
        offset += buffNumSamples * nChannels;
        LOG(INFO) << "Processed: " << offset;
    } while (buffNumSamples != 0);

    // save audio
    audioFile.printSummary();
    audioFile.save(FLAGS_output_file_path, AudioFileFormat::Wave);
    return EXIT_SUCCESS;
}
