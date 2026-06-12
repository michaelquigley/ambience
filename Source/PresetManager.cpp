#include "PresetManager.h"
#include "PluginProcessor.h"
#include "FactoryPresets.h"

PresetManager::PresetManager(FDNReverbAudioProcessor& p)
    : processor(p)
{
    seedFactoryPresetsIfNeeded();
    refreshPresetList();
}

// Extract the bundled factory presets to the user folder on first run.
// A marker file ensures that presets the user deletes stay deleted across launches.
void PresetManager::seedFactoryPresetsIfNeeded()
{
    auto folder = getPresetsFolder();
    auto marker = folder.getChildFile(".factory_installed_v1");
    if (marker.existsAsFile())
        return;

    for (int i = 0; i < FactoryPresets::namedResourceListSize; ++i)
    {
        const char* resName = FactoryPresets::namedResourceList[i];
        const char* origName = FactoryPresets::getNamedResourceOriginalFilename(resName);
        if (origName == nullptr)
            continue;

        juce::String filename(origName);
        if (!filename.endsWithIgnoreCase(kExtension))
            continue;

        auto outFile = folder.getChildFile(filename);
        if (outFile.existsAsFile())
            continue;

        int dataSize = 0;
        const char* data = FactoryPresets::getNamedResource(resName, dataSize);
        if (data == nullptr || dataSize <= 0)
            continue;

        outFile.replaceWithData(data, static_cast<size_t>(dataSize));
    }

    marker.create();
}

// ─────────────────────────────────────────────────────────────────────────────
//  ファイルシステム
// ─────────────────────────────────────────────────────────────────────────────
juce::File PresetManager::getPresetsFolder() const
{
   #if JUCE_LINUX || JUCE_BSD
    // userDocumentsDirectory honours XDG_DOCUMENTS_DIR, which on some setups points at
    // $HOME (e.g. XDG_DOCUMENTS_DIR="$HOME/") — that would dump the preset folder straight
    // into the home directory. Use the XDG data location instead: $XDG_DATA_HOME, or its
    // spec default ~/.local/share. JUCE has no special-location enum for this, so resolve
    // it by hand.
    auto xdgData = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", {}).trim();
    auto base = xdgData.isNotEmpty()
        ? juce::File(xdgData)
        : juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/share");
    auto folder = base.getChildFile(kSubFolder);
   #else
    // Windows/macOS: Documents/Ambience/Presets — a visible, user-managed location.
    auto folder = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory)
        .getChildFile(kSubFolder);
   #endif
    if (!folder.exists())
        folder.createDirectory();
    return folder;
}

juce::File PresetManager::getPresetFile(const juce::String& name) const
{
    return getPresetsFolder().getChildFile(name + kExtension);
}

void PresetManager::refreshPresetList()
{
    presetNames.clear();
    auto files = getPresetsFolder().findChildFiles(
        juce::File::findFiles, false,
        juce::String("*") + kExtension);
    files.sort();
    for (const auto& f : files)
        presetNames.add(f.getFileNameWithoutExtension());
}

// ─────────────────────────────────────────────────────────────────────────────
//  保存
// ─────────────────────────────────────────────────────────────────────────────
//  PluginProcessor の getStateInformation() をそのまま利用する。
//  これにより PluginProcessor の変更が一切不要になる。
// ─────────────────────────────────────────────────────────────────────────────
// ─── 変更後 ───
bool PresetManager::savePreset(const juce::String& name)
{
    if (name.isEmpty()) return false;
    // ★ 修正: getStateInformation の前に名前を Processor に通知する
    // これにより getStateInformation が正しい名前を ValueTree に書き込める
    processor.setLastSavedPresetName(name);
    juce::MemoryBlock data;
    processor.getStateInformation(data);

    auto file = getPresetFile(name);
    if (!file.replaceWithData(data.getData(), data.getSize()))
        return false;

    currentPresetName = name;
    refreshPresetList();
    if (onPresetListChanged) onPresetListChanged();
    if (onPresetLoaded)      onPresetLoaded(name);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ロード
// ─────────────────────────────────────────────────────────────────────────────
//  PluginProcessor の setStateInformation() をそのまま利用する。
//  setStateInformation() 内で paramsNeedUpdate=true が設定されるため、
//  次の processBlock で Engine に新しいパラメータが送られる。
// ─────────────────────────────────────────────────────────────────────────────
bool PresetManager::loadPreset(const juce::String& name)
{
    auto file = getPresetFile(name);
    if (!file.exists()) return false;

    juce::MemoryBlock data;
    if (!file.loadFileAsData(data)) return false;

    // ─── Mix lock ───
    // ロック中はプリセット適用が Wet/Dry を上書きしないよう、正規化値を退避する。
    // ロックフラグ自体も退避する: プリセットファイルに古い mixLocked が
    // 含まれていても、ライブのロック状態が切り替わらないようにするため。
    const bool lock = processor.isMixLocked();
    float wet01 = 0.0f, dry01 = 0.0f;
    auto* wetParam = processor.apvts.getParameter(FDNReverb::ParamID::WetLevel);
    auto* dryParam = processor.apvts.getParameter(FDNReverb::ParamID::DryLevel);
    if (lock)
    {
        if (wetParam != nullptr) wet01 = wetParam->getValue();
        if (dryParam != nullptr) dry01 = dryParam->getValue();
    }

    processor.setStateInformation(data.getData(), static_cast<int>(data.getSize()));

    // setStateInformation はプリセット内の mixLocked を読み込んでしまうので、
    // ライブのロック状態を必ず復元する。
    processor.setMixLocked(lock);
    if (lock)
    {
        // ホストと UI のノブに反映させるため setValueNotifyingHost で書き戻す。
        if (wetParam != nullptr) wetParam->setValueNotifyingHost(wet01);
        if (dryParam != nullptr) dryParam->setValueNotifyingHost(dry01);
    }

    currentPresetName = name;
    if (onPresetLoaded) onPresetLoaded(name);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  削除
// ─────────────────────────────────────────────────────────────────────────────
bool PresetManager::deletePreset(const juce::String& name)
{
    auto file = getPresetFile(name);
    if (!file.exists()) return false;

    if (!file.deleteFile()) return false;

    if (currentPresetName == name)
        currentPresetName.clear();

    refreshPresetList();
    if (onPresetListChanged) onPresetListChanged();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ナビゲーション
// ─────────────────────────────────────────────────────────────────────────────
int PresetManager::getCurrentPresetIndex() const noexcept
{
    return presetNames.indexOf(currentPresetName);
}

void PresetManager::loadPrevPreset()
{
    if (presetNames.isEmpty()) return;
    int idx = getCurrentPresetIndex();
    if (idx <= 0)
        idx = presetNames.size();
    loadPreset(presetNames[idx - 1]);
}

void PresetManager::loadNextPreset()
{
    if (presetNames.isEmpty()) return;
    int idx = getCurrentPresetIndex();
    if (idx < 0 || idx >= presetNames.size() - 1)
        idx = -1;
    loadPreset(presetNames[idx + 1]);
}