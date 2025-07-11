/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

class IOSInlineMediaControls extends InlineMediaControls
{

    constructor(options = {})
    {
        options.layoutTraits = new IOSLayoutTraits(LayoutTraits.Mode.Inline);

        super(options);

        this.element.classList.add("ios");

        this._updateGestureRecognizers();
    }

    // Public

    get showsStartButton()
    {
        return super.showsStartButton;
    }

    set showsStartButton(flag)
    {
        super.showsStartButton = flag;
        this._updateGestureRecognizers();
    }

    get visible()
    {
        return super.visible;
    }

    set visible(flag)
    {
        super.visible = flag;
        this._updateGestureRecognizers();
    }

    // Protected

    layout()
    {
        if (this.timeControl) {
            this.timeControl.scrubber.allowsRelativeScrubbing = this._shouldUseMultilineLayout;
            this.timeControl.scrubber.knobStyle = this._shouldUseMultilineLayout ? Slider.KnobStyle.None : Slider.KnobStyle.Circle;
        }

        if (this.playPauseButton)
            this.playPauseButton.scaleFactor = this._shouldUseMultilineLayout ? 3 : 1;

        if (this.skipForwardButton)
            this.skipForwardButton.scaleFactor = this._shouldUseMultilineLayout ? 2 : 1;

        if (this.skipBackButton)
            this.skipBackButton.scaleFactor = this._shouldUseMultilineLayout ? 2 : 1;

        if (this._topLeftControlsBarContainer) {
            this._topLeftControlsBarContainer.leftMargin = this._shouldUseMultilineLayout ? 2 : ButtonsContainer.Defaults.LeftMargin;
            this._topLeftControlsBarContainer.rightMargin = this._shouldUseMultilineLayout ? 2 : ButtonsContainer.Defaults.RightMargin;
        }

        if (this._topRightControlsBarContainer) {
            this._topRightControlsBarContainer.leftMargin = this._shouldUseMultilineLayout ? 2 : ButtonsContainer.Defaults.LeftMargin;
            this._topRightControlsBarContainer.rightMargin = this._shouldUseMultilineLayout ? 2 : ButtonsContainer.Defaults.RightMargin;
        }

        if (this.leftContainer)
            this.leftContainer.leftMargin = this._shouldUseMultilineLayout ? 2 : ButtonsContainer.Defaults.LeftMargin;
        if (this.rightContainer)
            this.rightContainer.rightMargin = this._shouldUseMultilineLayout ? 8 : ButtonsContainer.Defaults.RightMargin;

        if (this._centerControlsBarContainer)
            this._centerControlsBarContainer.buttonMargin = this._shouldUseMultilineLayout ? 48 : ButtonsContainer.Defaults.ButtonMargin;

        if (this.topLeftControlsBar)
            this.topLeftControlsBar.hasBackgroundTint = !this._shouldUseMultilineLayout;
        if (this.topRightControlsBar)
            this.topRightControlsBar.hasBackgroundTint = !this._shouldUseMultilineLayout;
        if (this.centerControlsBar)
            this.centerControlsBar.hasBackgroundTint = !this._shouldUseMultilineLayout;
        if (this.bottomControlsBar)
            this.bottomControlsBar.hasBackgroundTint = !this._shouldUseMultilineLayout;

        super.layout();

        if (this.playPauseButton?.style === Button.Styles.Corner)
            this.playPauseButton.scaleFactor = 1;
    }

    centerContainerButtons() {
        if (this._shouldUseMultilineLayout)
            return [this.skipBackButton, this.playPauseButton, this.skipForwardButton];
        return [];
    }

    leftContainerButtons()
    {
        if (this._shouldUseMultilineLayout)
            return [];
        return [this.skipBackButton, this.playPauseButton, this.skipForwardButton];
    }

    droppableButtons()
    {
        let buttons = super.droppableButtons();
        if (this._shouldUseMultilineLayout) {
            buttons.delete(this.skipForwardButton);
            buttons.delete(this.skipBackButton);
        }
        return buttons;
    }

    gestureRecognizerStateDidChange(recognizer)
    {
        if (recognizer.state === GestureRecognizer.States.Cancelled) {
            // The only way to enter a `Cancelled` state is to disable the gesture recognizer when
            // there are active touches. Since the gesture recognizer is now disabled (which clears
            // active touches and removes event listeners), don't bother handling the state change.
            return;
        }

        if (recognizer === this._pinchGestureRecognizer)
            this._pinchGestureRecognizerStateDidChange(recognizer);
        else if (recognizer === this._tapGestureRecognizer)
            this._tapGestureRecognizerStateDidChange(recognizer);
    }

    // Private

    get _shouldUseMultilineLayout()
    {
        return !this._shouldUseSingleBarLayout && !this._shouldUseAudioLayout;
    }

    _updateGestureRecognizers()
    {
        const shouldListenToPinches = this.visible;
        const shouldListenToTaps = this.visible && this.showsStartButton;

        if (shouldListenToPinches && !this._pinchGestureRecognizer)
            this._pinchGestureRecognizer = new PinchGestureRecognizer(this.element, this);
        else if (!shouldListenToPinches && this._pinchGestureRecognizer) {
            this._pinchGestureRecognizer.enabled = false;
            delete this._pinchGestureRecognizer;
        }

        if (shouldListenToTaps && !this._tapGestureRecognizer)
            this._tapGestureRecognizer = new TapGestureRecognizer(this.element, this);
        else if (!shouldListenToTaps && this._tapGestureRecognizer) {
            this._tapGestureRecognizer.enabled = false;
            delete this._tapGestureRecognizer;
        }
    }

    _pinchGestureRecognizerStateDidChange(recognizer)
    {
        console.assert(this.visible);
        if (recognizer.state !== GestureRecognizer.States.Recognized && recognizer.state !== GestureRecognizer.States.Changed)
            return;

        if (recognizer.scale > IOSInlineMediaControls.MinimumScaleToEnterFullscreen && this.delegate && typeof this.delegate.iOSInlineMediaControlsRecognizedPinchInGesture === "function")
            this.delegate.iOSInlineMediaControlsRecognizedPinchInGesture();
    }

    _tapGestureRecognizerStateDidChange(recognizer)
    {
        console.assert(this.visible && this.showsStartButton);
        if (recognizer.state === GestureRecognizer.States.Recognized && this.delegate && typeof this.delegate.iOSInlineMediaControlsRecognizedTapGesture === "function")
            this.delegate.iOSInlineMediaControlsRecognizedTapGesture();
    }

}

IOSInlineMediaControls.MinimumScaleToEnterFullscreen = 1.5;
