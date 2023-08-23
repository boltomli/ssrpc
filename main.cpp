#include "signal.h"
#include "proto/generated/sound.srpc.h"
#include "workflow/WFFacilities.h"
#include "srpc/rpc_types.h"

#include "AudioFile.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "soundtouch/SoundTouch.h"

DEFINE_int32(max_samples, 16777216, "Max samples per channel");
DEFINE_int32(port, 1412, "Port");

// using json = nlohmann::json;
using namespace soundtouch;
using namespace srpc;

static WFFacilities::WaitGroup wait_group(1);

class SoundServiceImpl : public Sound::Service
{
public:
	void Stretch(StretchRequest *req, StretchResponse *resp, RPCContext *ctx) override
	{
		ctx->set_json_preserve_proto_field_names(true);
		ctx->set_json_always_print_primitive_fields(true);

        // load audio
        AudioFile<float> audioFile;
        auto wave = req->wave();
        auto waveData = std::vector<unsigned char>(wave.begin(), wave.end());
        bool status = audioFile.loadFromMemory(waveData);
        if (!status) {
			ctx->set_http_code(400);
            LOG(ERROR) << "ERROR: File load error";
            return;
        }
        audioFile.printSummary();

        // sound stretch
        SoundTouch soundTouch;

        int nSampleRate = audioFile.getSampleRate();
        int nChannels = audioFile.getNumChannels();
        soundTouch.setSampleRate(nSampleRate);
        soundTouch.setChannels(nChannels);

        // change tempo
        soundTouch.setTempoChange((float)req->tempo());

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
        int targetSamples = nSamples * 100 / (100 + req->tempo());
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

        std::vector<uint8_t> fileData;
    
        auto bitDepth = audioFile.getBitDepth();
        auto iXMLChunk = audioFile.iXMLChunk;
        int32_t dataChunkSize = targetSamples * (nChannels * bitDepth / 8);
        int16_t audioFormat = bitDepth == 32 && std::is_floating_point_v<float> ? WavAudioFormat::IEEEFloat : WavAudioFormat::PCM;
        int32_t formatChunkSize = audioFormat == WavAudioFormat::PCM ? 16 : 18;
        int32_t iXMLChunkSize = static_cast<int32_t> (iXMLChunk.size());
        
        // -----------------------------------------------------------
        // HEADER CHUNK
        addStringToFileData (fileData, "RIFF");
        
        // The file size in bytes is the header chunk size (4, not counting RIFF and WAVE) + the format
        // chunk size (24) + the metadata part of the data chunk plus the actual data chunk size
        int32_t fileSizeInBytes = 4 + formatChunkSize + 8 + 8 + dataChunkSize;
        if (iXMLChunkSize > 0)
        {
            fileSizeInBytes += (8 + iXMLChunkSize);
        }

        addInt32ToFileData (fileData, fileSizeInBytes);
        
        addStringToFileData (fileData, "WAVE");
        
        // -----------------------------------------------------------
        // FORMAT CHUNK
        addStringToFileData (fileData, "fmt ");
        addInt32ToFileData (fileData, formatChunkSize); // format chunk size (16 for PCM)
        addInt16ToFileData (fileData, audioFormat); // audio format
        addInt16ToFileData (fileData, (int16_t)nChannels); // num channels
        addInt32ToFileData (fileData, (int32_t)nSampleRate); // sample rate
        
        int32_t numBytesPerSecond = (int32_t) ((nChannels * nSampleRate * bitDepth) / 8);
        addInt32ToFileData (fileData, numBytesPerSecond);
        
        int16_t numBytesPerBlock = nChannels * (bitDepth / 8);
        addInt16ToFileData (fileData, numBytesPerBlock);
        
        addInt16ToFileData (fileData, (int16_t)bitDepth);
        
        if (audioFormat == WavAudioFormat::IEEEFloat)
            addInt16ToFileData (fileData, 0); // extension size
        
        // -----------------------------------------------------------
        // DATA CHUNK
        addStringToFileData (fileData, "data");
        addInt32ToFileData (fileData, dataChunkSize);
        
        for (int i = 0; i < targetSamples; i++)
        {
            for (int channel = 0; channel < nChannels; channel++)
            {
                if (bitDepth == 8)
                {
                    uint8_t byte = sampleToUnsignedByte (audioFile.samples[channel][i]);
                    fileData.push_back (byte);
                }
                else if (bitDepth == 16)
                {
                    int16_t sampleAsInt = sampleToSixteenBitInt (audioFile.samples[channel][i]);
                    addInt16ToFileData (fileData, sampleAsInt);
                }
                else if (bitDepth == 24)
                {
                    int32_t sampleAsIntAgain = sampleToTwentyFourBitInt (audioFile.samples[channel][i]);
                    
                    uint8_t bytes[3];
                    bytes[2] = (uint8_t) (sampleAsIntAgain >> 16) & 0xFF;
                    bytes[1] = (uint8_t) (sampleAsIntAgain >>  8) & 0xFF;
                    bytes[0] = (uint8_t) sampleAsIntAgain & 0xFF;
                    
                    fileData.push_back (bytes[0]);
                    fileData.push_back (bytes[1]);
                    fileData.push_back (bytes[2]);
                }
                else if (bitDepth == 32)
                {
                    int32_t sampleAsInt;
                    
                    if (audioFormat == WavAudioFormat::IEEEFloat)
                        sampleAsInt = (int32_t) reinterpret_cast<int32_t&> (audioFile.samples[channel][i]);
                    else // assume PCM
                        sampleAsInt = sampleToThirtyTwoBitInt (audioFile.samples[channel][i]);
                    
                    addInt32ToFileData (fileData, sampleAsInt);
                }
                else
                {
                    ctx->set_http_code(400);
                    LOG(ERROR) << "ERROR: Trying to write a file with unsupported bit depth";
                    return;
                }
            }
        }
        
        // -----------------------------------------------------------
        // iXML CHUNK
        if (iXMLChunkSize > 0) 
        {
            addStringToFileData (fileData, "iXML");
            addInt32ToFileData (fileData, iXMLChunkSize);
            addStringToFileData (fileData, iXMLChunk);
        }
        
        // check that the various sizes we put in the metadata are correct
        if (fileSizeInBytes != static_cast<int32_t> (fileData.size() - 8) || dataChunkSize != (targetSamples * nChannels * (bitDepth / 8)))
        {
			ctx->set_http_code(400);
            LOG(ERROR) << "ERROR: couldn't save file";
            return;
        }

        resp->set_wave(std::string(fileData.begin(), fileData.end()));
	}
private:
    void addStringToFileData (std::vector<uint8_t>& fileData, std::string s)
    {
        for (size_t i = 0; i < s.length();i++)
            fileData.push_back ((uint8_t) s[i]);
    }

    void addInt32ToFileData (std::vector<uint8_t>& fileData, int32_t i)
    {
        uint8_t bytes[4];
        
        bytes[3] = (i >> 24) & 0xFF;
        bytes[2] = (i >> 16) & 0xFF;
        bytes[1] = (i >> 8) & 0xFF;
        bytes[0] = i & 0xFF;
        
        for (int i = 0; i < 4; i++)
            fileData.push_back (bytes[i]);
    }

    void addInt16ToFileData (std::vector<uint8_t>& fileData, int16_t i)
    {
        uint8_t bytes[2];
        
        bytes[1] = (i >> 8) & 0xFF;
        bytes[0] = i & 0xFF;
        
        fileData.push_back (bytes[0]);
        fileData.push_back (bytes[1]);
    }

    uint8_t sampleToUnsignedByte (float sample)
    {
        sample = clamp (sample, -1., 1.);
        sample = (sample + 1.) / 2.;
        return static_cast<uint8_t> (1 + (sample * 254));
    }

    int16_t sampleToSixteenBitInt (float sample)
    {
        sample = clamp (sample, -1., 1.);
        return static_cast<int16_t> (sample * 32767.);
    }

    int32_t sampleToTwentyFourBitInt (float sample)
    {
        sample = clamp (sample, -1., 1.);
        return static_cast<int32_t> (sample * 8388607.);
    }

    int32_t sampleToThirtyTwoBitInt (float sample)
    {
        // multiplying a float by a the max int32_t is problematic because
        // of roundng errors which can cause wrong values to come out, so
        // we use a different implementation here compared to other types
        if (sample >= 1.f)
            return std::numeric_limits<int32_t>::max();
        else if (sample <= -1.f)
            return std::numeric_limits<int32_t>::lowest() + 1; // starting at 1 preserves symmetry
        else
            return static_cast<int32_t> (sample * std::numeric_limits<int32_t>::max());
    }

    float clamp (float value, float minValue, float maxValue)
    {
        value = std::min (value, maxValue);
        value = std::max (value, minValue);
        return value;
    }
};

static void sig_handler(int signo)
{
	wait_group.done();
}

int main(int argc, char* argv[])
{
    gflags::ParseCommandLineFlags(&argc, &argv, true);

	GOOGLE_PROTOBUF_VERIFY_VERSION;
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	SRPCHttpServer server;
	SoundServiceImpl impl;

	server.add_service(&impl);

	if (server.start(FLAGS_port) == 0)
	{
		wait_group.wait();
		server.stop();
	}
	else
		perror("server start");

	google::protobuf::ShutdownProtobufLibrary();
	return EXIT_SUCCESS;
}
