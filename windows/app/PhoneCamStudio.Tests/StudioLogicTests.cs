using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using PhoneCamStudio.Interop;
using PhoneCamStudio.Models;
using PhoneCamStudio.Services;
using Xunit;

namespace PhoneCamStudio.Tests;

public class StudioLogicTests
{
    [Fact]
    public void VideoParamsInteropStructMatchesNativeAbiSize()
    {
        // 16 ints (64 bytes) + 27 floats (108 bytes) = 172 bytes
        Assert.Equal(172, Marshal.SizeOf<PC_VideoParams>());
    }

    [Fact]
    public void BuiltInPresetsContain16ValidProfilesWithRealAiAndShaderEffects()
    {
        Assert.Equal(16, PresetCatalog.BuiltInPresets.Count);
        bool sawAiBg = false, sawStyle = false, sawAr = false, sawFaceSculpt = false;
        foreach (var preset in PresetCatalog.BuiltInPresets)
        {
            Assert.False(string.IsNullOrWhiteSpace(preset.Id));
            Assert.False(string.IsNullOrWhiteSpace(preset.NameAr));
            Assert.False(string.IsNullOrWhiteSpace(preset.NameEn));
            Assert.True(preset.IsBuiltIn);
            if (preset.Settings.BgMode > 0) sawAiBg = true;
            if (preset.Settings.StyleFilterMode > 0) sawStyle = true;
            if (preset.Settings.ArFilterMode > 0) sawAr = true;
            if (preset.Settings.FaceSlim > 0f && preset.Settings.EyeEnlarge > 0f) sawFaceSculpt = true;
        }
        Assert.True(sawAiBg);
        Assert.True(sawStyle);
        Assert.True(sawAr);
        Assert.True(sawFaceSculpt);
    }

    [Fact]
    public void CustomPresetsRoundtripToJsonFile()
    {
        string tempFile = Path.Combine(Path.GetTempPath(), $"phonecam_presets_{Guid.NewGuid():N}.json");
        try
        {
            var custom = new FilterPreset
            {
                NameAr = "فلتر تجريبي",
                NameEn = "Test Filter",
                IsBuiltIn = false,
                Settings = new ColorBeautySettings { SkinSmooth = 0.75f, Temperature = 0.25f, Contrast = 1.2f }
            };
            PresetCatalog.SaveCustomPresets(new[] { PresetCatalog.BuiltInPresets[0], custom }, tempFile);
            var loaded = PresetCatalog.LoadCustomPresets(tempFile);
            Assert.Single(loaded);
            Assert.Equal("Test Filter", loaded[0].NameEn);
            Assert.Equal(0.75f, loaded[0].Settings.SkinSmooth, 2);
            Assert.False(loaded[0].IsBuiltIn);
        }
        finally
        {
            if (File.Exists(tempFile)) File.Delete(tempFile);
        }
    }

    [Fact]
    public void PerCameraMirrorMemoryKeepsFrontAndBackIndependent()
    {
        string tempFile = Path.Combine(Path.GetTempPath(), $"phonecam_mirror_{Guid.NewGuid():N}.json");
        try
        {
            var mgr = new MirrorSettingsManager(tempFile);
            mgr.OnCameraFacingChanged(isFront: true);
            mgr.CurrentTransform.FlipH = true;
            mgr.CurrentTransform.ManualRotationDeg = 90;

            mgr.OnCameraFacingChanged(isFront: false);
            mgr.CurrentTransform.FlipH = false;
            mgr.CurrentTransform.ManualRotationDeg = 0;
            mgr.Save();

            var reloaded = new MirrorSettingsManager(tempFile);
            reloaded.OnCameraFacingChanged(isFront: true);
            Assert.True(reloaded.CurrentTransform.FlipH);
            Assert.Equal(90, reloaded.CurrentTransform.ManualRotationDeg);

            reloaded.OnCameraFacingChanged(isFront: false);
            Assert.False(reloaded.CurrentTransform.FlipH);
            Assert.Equal(0, reloaded.CurrentTransform.ManualRotationDeg);
        }
        finally
        {
            if (File.Exists(tempFile)) File.Delete(tempFile);
        }
    }

    [Fact]
    public void PlatformMirrorPresetsConfigureOutputAndGuidance()
    {
        string tempFile = Path.Combine(Path.GetTempPath(), $"phonecam_plat_{Guid.NewGuid():N}.json");
        try
        {
            var mgr = new MirrorSettingsManager(tempFile);
            mgr.ToggleOutputFlipH();
            Assert.True(mgr.CurrentTransform.FlipH);

            mgr.ApplyPlatformPreset(PlatformMirrorPreset.WhatsApp);
            Assert.False(mgr.CurrentTransform.FlipH);
            Assert.Equal(0, mgr.CurrentTransform.FramingMode);
            Assert.Contains("واتساب", MirrorSettingsManager.GetPresetGuidance(PlatformMirrorPreset.WhatsApp, true));
            Assert.Contains("WhatsApp", MirrorSettingsManager.GetPresetGuidance(PlatformMirrorPreset.WhatsApp, false));
        }
        finally
        {
            if (File.Exists(tempFile)) File.Delete(tempFile);
        }
    }

    [Fact]
    public void LocalizationSwitchesBetweenArabicRtlAndEnglishLtr()
    {
        var loc = new LocalizationService();
        Assert.True(loc.IsArabic);
        Assert.Equal(FlowDirection.RightToLeft, loc.FlowDirection);
        Assert.NotEqual("StartVCam", loc["StartVCam"]);

        loc.IsArabic = false;
        Assert.Equal(FlowDirection.LeftToRight, loc.FlowDirection);
        Assert.Equal("Start Virtual Camera", loc["StartVCam"]);
    }
}
