
#include "pch.hpp"
#include "Outputs.h"
#include <musikcore/plugin/PluginFactory.h>
#include <musikcore/support/Preferences.h>
#include <musikcore/support/PreferenceKeys.h>

#include <atomic>
#include <algorithm>

using namespace musik::core;
using namespace musik::core::audio;
using namespace musik::core::sdk;
using namespace musik::core::prefs;

using Output = std::shared_ptr<IOutput>;
using OutputList = std::vector<Output>;

#if defined(WIN32)
static const std::string defaultOutput = "WASAPI";
#elif defined(__APPLE__)
static const std::string defaultOutput = "CoreAudio";
#elif defined(__FreeBSD__) || defined (__NetBSD__) || defined (__OpenBSD__) || defined (__DragonFly__)
static const std::string defaultOutput = "sndio";
#else
static const std::string defaultOutput = "PulseAudio";
#endif

#define LOWER(x) std::transform(x.begin(), x.end(), x.begin(), tolower);

class NoOutput: public IOutput {
    public:
        void Release() override { delete this; }
        void Pause() override { }
        void Resume() override { }
        void SetVolume(double volume) override { this->volume = volume; }
        double GetVolume() override { return this->volume; }
        void Stop() override { }
        OutputState Play(IBuffer *buffer, IBufferProvider *provider) override { return OutputState::InvalidState; }
        void Drain() override { }
        double Latency() override { return 0.0; }
        const char* Name() override { return "NoOutput"; }
        IDeviceList* GetDeviceList() override { return nullptr; }
        bool SetDefaultDevice(const char* deviceId) override { return false; }
        IDevice* GetDefaultDevice() override { return nullptr; }
        int GetDefaultSampleRate() override { return -1; }
    private:
        double volume{ 1.0f };
};

namespace musik {
    namespace core {
        namespace audio {
            namespace outputs {
                using ReleaseDeleter = PluginFactory::ReleaseDeleter<IOutput>;
                using NullDeleter = PluginFactory::NullDeleter<IOutput>;

                static inline void release(OutputList outputs) {
                    for (auto output : outputs) {
                        output->Release();
                    }
                }

                template <typename D>
                OutputList queryOutputs() {
                    OutputList result = PluginFactory::Instance()
                        .QueryInterface<IOutput, D>("GetAudioOutput");

                    std::sort(
                        result.begin(),
                        result.end(),
                        [](Output left, Output right) -> bool {
                            std::string l = left->Name();
                            LOWER(l);
                            std::string r = right->Name();
                            LOWER(r);
                            return l < r;
                        });

                    return result;
                }

                static Output findByName(const std::string& name, const OutputList& list) {
                    if (name.size()) {
                        auto it = list.begin();
                        while (it != list.end()) {
                            if ((*it)->Name() == name) {
                                return (*it);
                            }
                            ++it;
                        }
                    }
                    return Output();
                }

                OutputList GetAllOutputs() {
                    return queryOutputs<ReleaseDeleter>();
                }

                void SelectOutput(std::shared_ptr<musik::core::sdk::IOutput> output) {
                    SelectOutput(output.get());
                }

                void SelectOutput(musik::core::sdk::IOutput* output) {
                    if (output) {
                        std::shared_ptr<Preferences> prefs =
                            Preferences::ForComponent(components::Playback);

                        prefs->SetString(keys::OutputPlugin, output->Name());
                    }
                }

                size_t GetOutputCount() {
                    return queryOutputs<ReleaseDeleter>().size();
                }

                musik::core::sdk::IOutput* GetUnmanagedOutput(size_t index) {
                    auto all = queryOutputs<NullDeleter>();
                    if (!all.size()) {
                        return new NoOutput();
                    }
                    auto output = all[index].get();
                    all.erase(all.begin() + index);
                    release(all);
                    return output;
                }

                musik::core::sdk::IOutput* GetUnmanagedOutput(const std::string& name) {
                    auto all = queryOutputs<NullDeleter>();
                    IOutput* output = nullptr;
                    for (size_t i = 0; i < all.size(); i++) {
                        if (all[i]->Name() == name) {
                            output = all[i].get();
                            all.erase(all.begin() + i);
                            break;
                        }
                    }
                    release(all);
                    return output == nullptr ? new NoOutput() : output;
                }

                musik::core::sdk::IOutput* GetUnmanagedSelectedOutput() {
                    IOutput* output = nullptr;

                    OutputList plugins = queryOutputs<NullDeleter>();

                    if (plugins.size()) {
                        std::shared_ptr<Preferences> prefs =
                            Preferences::ForComponent(components::Playback);

                        const std::string name = prefs->GetString(keys::OutputPlugin);

                        for (size_t i = 0; i < plugins.size(); i++) {
                            if (plugins[i]->Name() == name) {
                                output = plugins[i].get();
                                plugins.erase(plugins.begin() + i);
                                break;
                            }
                        }

                        if (!output) {
                            output = plugins[0].get();
                            plugins.erase(plugins.begin());
                        }
                    }

                    release(plugins);

                    return output == nullptr ? new NoOutput() : output;
                }

                Output SelectedOutput() {
                    std::shared_ptr<Preferences> prefs =
                        Preferences::ForComponent(components::Playback);

                    const OutputList plugins = queryOutputs<ReleaseDeleter>();

                    if (plugins.size()) {
                        /* try to find the user selected plugin first */
                        Output result = findByName(prefs->GetString(keys::OutputPlugin), plugins);

                        if (!result) {
                            /* fall back to the default */
                            result = findByName(defaultOutput, plugins);
                        }

                        if (!result) {
                            /* no default? ugh, return the first one. */
                            result = plugins[0];
                        }

                        return result;
                    }

                    return Output(new NoOutput());
                }
            }
        }
    }
}
