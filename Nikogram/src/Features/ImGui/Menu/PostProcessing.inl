// Shared by the native/Nullcore page and Moonlit Visuals editor.
static void PostProcessingSettings()
{
    using namespace ImGui;namespace V=Vars::Visuals::PostProcessing;
    FToggle(V::Enabled);
    FText(PostFX::StatusText());
    FText("Scene effects only. HUD and menus stay sharp.");
    if(FButton("Disable all post effects"))FSet(V::Enabled,false);
    if(Section("Light & clarity"))
    {
        FToggle(V::Bloom);if(FGet(V::Bloom)){FSlider(V::BloomStrength);FSlider(V::BloomRadius);FSlider(V::BloomThreshold);}
        FToggle(V::AntiAliasing);if(FGet(V::AntiAliasing))FSlider(V::AAStrength);
        FToggle(V::Sharpening);if(FGet(V::Sharpening))FSlider(V::Sharpness);
        FToggle(V::Exposure);if(FGet(V::Exposure)){FSlider(V::ExposureEV);FSlider(V::HighlightCompression);}
    }EndSection();
    if(Section("Motion & depth"))
    {
        FToggle(V::MotionBlur);if(FGet(V::MotionBlur)){FSlider(V::TurnBlur);FSlider(V::ForwardBlur);}
        FToggle(V::DepthOfField);if(FGet(V::DepthOfField)){FToggle(V::Autofocus);FSlider(V::FocusDistance);FSlider(V::FocusRange);FSlider(V::BlurRadius);}
        FText("DOF requires native scene-depth resolve support.");
        if(FGet(V::DepthOfField))
            FText(PostFX::depthAvailable.load()?"Scene depth available.":"Depth unavailable or not yet rendered. DOF is skipped.");
    }EndSection();
    if(Section("Colour grading"))
    {
        FToggle(V::ColourGrading);
        if(FGet(V::ColourGrading))
        {
            FSlider(V::GradeMix);FDropdown(V::Preset);FSlider(V::PresetMix);
            FSlider(V::Saturation);FSlider(V::Contrast);FSlider(V::Warmth);FSlider(V::Tint);FSlider(V::Shadows);FSlider(V::Highlights);
        }
    }EndSection();
    if(Section("Stylised shading"))
    {
        FToggle(V::Posterisation);if(FGet(V::Posterisation))FSlider(V::PosterLevels);
        FToggle(V::Halftone);if(FGet(V::Halftone)){FSlider(V::DotSize);FSlider(V::DotStrength);}
        FToggle(V::ComicOutlines);if(FGet(V::ComicOutlines))FSlider(V::InkStrength);
    }EndSection();
    if(Section("Lens & film"))
    {
        FToggle(V::Vignette);if(FGet(V::Vignette))FSlider(V::VignetteStrength);
        FToggle(V::FilmGrain);if(FGet(V::FilmGrain))FSlider(V::GrainStrength);
        FToggle(V::LensDirt);if(FGet(V::LensDirt))FSlider(V::DirtStrength);
        FToggle(V::ChromaticAberration);if(FGet(V::ChromaticAberration))FSlider(V::ChromaticStrength);
        FToggle(V::Letterboxing);if(FGet(V::Letterboxing))FSlider(V::BarSize);
    }EndSection();
}
