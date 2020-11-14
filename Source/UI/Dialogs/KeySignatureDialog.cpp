/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "KeySignatureDialog.h"

#include "KeySignaturesSequence.h"
#include "ProjectNode.h"
#include "ProjectMetadata.h"
#include "Temperament.h"
#include "Transport.h"

#include "SerializationKeys.h"
#include "CommandIDs.h"
#include "Config.h"

class ScalePreviewThread final : public Thread
{
public:

    ScalePreviewThread(const Transport &transport, Array<int> &&s) :
        Thread("ScalePreview"),
        transport(transport),
        sequence(s) {}

    void run() override
    {
        for (const auto key : this->sequence)
        {
            if (this->threadShouldExit())
            {
                this->transport.stopSound({});
                return;
            }

            this->transport.stopSound({});
            Thread::wait(25);
            this->transport.previewKey({}, 1, key, 0.5f);

            int c = 400;
            while (c > 0)
            {
                const auto a = Time::getMillisecondCounter();
                Thread::wait(25);
                const auto b = Time::getMillisecondCounter();
                c -= (b - a);

                if (this->threadShouldExit())
                {
                    this->transport.stopSound({});
                    return;
                }
            }
        }

        this->transport.stopSound({});
    }

private:

    const Transport &transport;
    const Array<int> sequence;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScalePreviewThread)
};

static inline const Temperament::Period &getPeriod(ProjectNode &project)
{
    return project.getProjectInfo()->getTemperament()->getPeriod();
}

KeySignatureDialog::KeySignatureDialog(ProjectNode &project, KeySignaturesSequence *keySequence,
    const KeySignatureEvent &editedEvent, bool shouldAddNewEvent, float targetBeat) :
    transport(project.getTransport()),
    originalEvent(editedEvent),
    originalSequence(keySequence),
    project(project),
    addsNewEvent(shouldAddNewEvent)
{
    this->comboPrimer = make<MobileComboBox::Primer>();
    this->addAndMakeVisible(this->comboPrimer.get());

    this->messageLabel = make<Label>();
    this->addAndMakeVisible(this->messageLabel.get());
    this->messageLabel->setFont({ 21.f });
    this->messageLabel->setJustificationType(Justification::centred);

    this->removeEventButton = make<TextButton>();
    this->addAndMakeVisible(this->removeEventButton.get());
    this->removeEventButton->onClick = [this]()
    {
        if (this->addsNewEvent)
        {
            this->cancelAndDisappear();
        }
        else
        {
            this->removeEvent();
            this->dismiss();
        }
    };

    this->okButton = make<TextButton>();
    this->addAndMakeVisible(this->okButton.get());
    this->okButton->onClick = [this]()
    {
        if (this->scaleNameEditor->getText().isNotEmpty())
        {
            this->dismiss();
        }
    };

    this->keySelector = make<KeySelector>(getPeriod(project));
    this->addAndMakeVisible(this->keySelector.get());

    this->scaleEditor = make<ScaleEditor>();
    this->addAndMakeVisible(this->scaleEditor.get());

    this->playButton = make<PlayButton>(this);
    this->addAndMakeVisible(this->playButton.get());

    this->scaleNameEditor = make<TextEditor>();
    this->addAndMakeVisible(this->scaleNameEditor.get());
    this->scaleNameEditor->setMultiLine(false);
    this->scaleNameEditor->setReturnKeyStartsNewLine(false);
    this->scaleNameEditor->setReadOnly(false);
    this->scaleNameEditor->setScrollbarsShown(true);
    this->scaleNameEditor->setCaretVisible(true);
    this->scaleNameEditor->setPopupMenuEnabled(true);

    this->scaleNameEditor->addListener(this);
    this->scaleNameEditor->setFont({ 21.f });

    const auto periodSize = getPeriod(project).size();
    const auto allScales = App::Config().getScales()->getAll();
    for (const auto &scale : allScales)
    {
        if (scale->getBasePeriod() == periodSize)
        {
            this->scales.add(scale);
        }
    }

    this->transport.stopPlaybackAndRecording();

    jassert(this->originalSequence != nullptr);
    jassert(this->addsNewEvent || this->originalEvent.getSequence() != nullptr);

    if (this->addsNewEvent)
    {
        Random r;
        const auto i = r.nextInt(this->scales.size());
        this->rootKey = 0;
        this->scale = this->scales[i];
        this->scaleEditor->setScale(this->scale);
        this->keySelector->setSelectedKey(this->rootKey);
        this->scaleNameEditor->setText(this->scale->getLocalizedName());
        this->originalEvent = KeySignatureEvent(this->originalSequence, this->scale, targetBeat, this->rootKey);

        this->originalSequence->checkpoint();
        this->originalSequence->insert(this->originalEvent, true);

        this->messageLabel->setText(TRANS(I18n::Dialog::keySignatureAddCaption), dontSendNotification);
        this->okButton->setButtonText(TRANS(I18n::Dialog::add));
        this->removeEventButton->setButtonText(TRANS(I18n::Dialog::cancel));
    }
    else
    {
        this->rootKey = this->originalEvent.getRootKey();
        this->scale = this->originalEvent.getScale();
        this->scaleEditor->setScale(this->scale);
        this->keySelector->setSelectedKey(this->rootKey);
        this->scaleNameEditor->setText(this->scale->getLocalizedName(), dontSendNotification);

        this->messageLabel->setText(TRANS(I18n::Dialog::keySignatureEditCaption), dontSendNotification);
        this->removeEventButton->setButtonText(TRANS(I18n::Dialog::delete_));
        this->okButton->setButtonText(TRANS(I18n::Dialog::apply));
    }

    this->messageLabel->setInterceptsMouseClicks(false, false);

    static constexpr auto keyButtonSize = 34;
    this->setSize(keyButtonSize * periodSize + this->getPaddingAndMarginTotal(), 260);

    this->updatePosition();
    this->updateOkButtonState();

    MenuPanel::Menu menu;
    for (int i = 0; i < this->scales.size(); ++i)
    {
        const auto &s = this->scales.getUnchecked(i);
        menu.add(MenuItem::item(Icons::ellipsis, CommandIDs::SelectScale + i, s->getLocalizedName()));
    }
    this->comboPrimer->initWith(this->scaleNameEditor.get(), menu);
}

KeySignatureDialog::~KeySignatureDialog()
{
    if (this->scalePreviewThread != nullptr)
    {
        this->scalePreviewThread->stopThread(500);
    }

    this->comboPrimer->cleanup();
    this->transport.stopPlayback();
    this->scaleNameEditor->removeListener(this);
}

void KeySignatureDialog::resized()
{
    this->comboPrimer->setBounds(this->getContentBounds(0.5f));
    this->messageLabel->setBounds(this->getCaptionBounds());

    const auto buttonsBounds(this->getButtonsBounds());
    const auto buttonWidth = buttonsBounds.getWidth() / 2;

    this->okButton->setBounds(buttonsBounds.withTrimmedLeft(buttonWidth));
    this->removeEventButton->setBounds(buttonsBounds.withTrimmedRight(buttonWidth + 1));

    this->keySelector->setBounds(this->getRowBounds(0.2f, DialogBase::textEditorHeight));
    this->scaleEditor->setBounds(this->getRowBounds(0.5f, DialogBase::textEditorHeight));

    static constexpr auto scaleEditorMargin = 4;
    static constexpr auto playButtonSize = 40;
    const auto scaleEditorRow = this->getRowBounds(0.8f, DialogBase::textEditorHeight, scaleEditorMargin);
    this->scaleNameEditor->setBounds(scaleEditorRow.withTrimmedRight(playButtonSize));
    this->playButton->setBounds(scaleEditorRow.withTrimmedLeft(this->scaleNameEditor->getWidth()));
}

void KeySignatureDialog::parentHierarchyChanged()
{
    this->updatePosition();
}

void KeySignatureDialog::parentSizeChanged()
{
    this->updatePosition();
}

void KeySignatureDialog::handleCommandMessage(int commandId)
{
    if (commandId == CommandIDs::DismissModalDialogAsync)
    {
        this->cancelAndDisappear();
    }
    else if (commandId == CommandIDs::TransportPlaybackStart)
    {
        const auto temperament =
            this->project.getProjectInfo()->getTemperament();

        // scale preview: simply play it forward and backward
        auto scaleKeys = this->scale->getUpScale();
        scaleKeys.addArray(this->scale->getDownScale());
        for (int i = 0; i < scaleKeys.size(); ++i)
        {
            const auto key = scaleKeys.getUnchecked(i);
            scaleKeys.getReference(i) = temperament->getMiddleC() + this->rootKey + key;
        }
        
        if (this->scalePreviewThread != nullptr)
        {
            this->scalePreviewThread->stopThread(500);
        }

        this->scalePreviewThread = make<ScalePreviewThread>(this->transport, move(scaleKeys));
        this->scalePreviewThread->startThread(5);

        this->playButton->setPlaying(true);
    }
    else if (commandId == CommandIDs::TransportStop)
    {
        if (this->scalePreviewThread != nullptr)
        {
            this->scalePreviewThread->stopThread(500);
        }

        this->playButton->setPlaying(false);
    }
    else
    {
        const int scaleIndex = commandId - CommandIDs::SelectScale;
        if (scaleIndex >= 0 && scaleIndex < this->scales.size())
        {
            this->playButton->setPlaying(false);
            this->scale = this->scales[scaleIndex];
            this->scaleEditor->setScale(this->scale);

            this->scaleNameEditor->grabKeyboardFocus();
            this->scaleNameEditor->setText(this->scale->getLocalizedName(), false);
            const auto newEvent = this->originalEvent
                .withRootKey(this->rootKey).withScale(this->scale);

            this->sendEventChange(newEvent);
        }
    }
}

void KeySignatureDialog::inputAttemptWhenModal()
{
    this->postCommandMessage(CommandIDs::DismissModalDialogAsync);
}

UniquePointer<Component> KeySignatureDialog::editingDialog(ProjectNode &project,
    const KeySignatureEvent &event)
{
    return make<KeySignatureDialog>(project,
        static_cast<KeySignaturesSequence *>(event.getSequence()), event, false, 0.f);
}

UniquePointer<Component> KeySignatureDialog::addingDialog(ProjectNode &project,
    KeySignaturesSequence *annotationsLayer, float targetBeat)
{
    return make<KeySignatureDialog>(project,
        annotationsLayer, KeySignatureEvent(), true, targetBeat);
}

void KeySignatureDialog::updateOkButtonState()
{
    const bool textIsEmpty = this->scaleNameEditor->getText().isEmpty();
    this->okButton->setAlpha(textIsEmpty ? 0.5f : 1.f);
    this->okButton->setEnabled(!textIsEmpty);
}

void KeySignatureDialog::sendEventChange(const KeySignatureEvent &newEvent)
{
    jassert(this->originalSequence != nullptr);

    if (this->addsNewEvent)
    {
        this->originalSequence->undo();
        this->originalSequence->insert(newEvent, true);
        this->originalEvent = newEvent;
    }
    else
    {
        if (this->hasMadeChanges)
        {
            this->originalSequence->undo();
            this->hasMadeChanges = false;
        }

        this->originalSequence->checkpoint();
        this->originalSequence->change(this->originalEvent, newEvent, true);
        this->hasMadeChanges = true;
    }
}

void KeySignatureDialog::removeEvent()
{
    jassert(this->originalSequence != nullptr);

    if (this->addsNewEvent)
    {
        this->originalSequence->undo();
    }
    else
    {
        if (this->hasMadeChanges)
        {
            this->originalSequence->undo();
            this->hasMadeChanges = false;
        }

        this->originalSequence->checkpoint();
        this->originalSequence->remove(this->originalEvent, true);
        this->hasMadeChanges = true;
    }
}

void KeySignatureDialog::cancelAndDisappear()
{
    jassert(this->originalSequence != nullptr);

    if (this->addsNewEvent || this->hasMadeChanges)
    {
        this->originalSequence->undo();
    }

    this->dismiss();
}

void KeySignatureDialog::previewNote(int keyRelative) const
{
    const auto temperament = this->project.getProjectInfo()->getTemperament();
    const int key = temperament->getMiddleC() + keyRelative;
    this->transport.stopSound({});
    this->transport.previewKey({}, 1, key, 0.5f);
}

//===----------------------------------------------------------------------===//
// KeySelector::Listener
//===----------------------------------------------------------------------===//

void KeySignatureDialog::onKeyChanged(int key)
{
    if (this->rootKey != key)
    {
        this->rootKey = key;
        const auto newEvent = this->originalEvent
            .withRootKey(key).withScale(this->scale);

        this->sendEventChange(newEvent);
    }
}

void KeySignatureDialog::onRootKeyPreview(int key)
{
    this->previewNote(key);
}

//===----------------------------------------------------------------------===//
// ScaleEditor::Listener
//===----------------------------------------------------------------------===//

void KeySignatureDialog::onScaleChanged(const Scale::Ptr scale)
{
    if (!this->scale->isEquivalentTo(scale))
    {
        this->scale = scale;

        // Update name, if found equivalent:
        for (int i = 0; i < this->scales.size(); ++i)
        {
            const auto &s = this->scales.getUnchecked(i);
            if (s->isEquivalentTo(scale))
            {
                this->scaleNameEditor->setText(s->getLocalizedName());
                this->scaleEditor->setScale(s);
                this->scale = s;
                break;
            }
        }

        const auto newEvent = this->originalEvent
            .withRootKey(this->rootKey).withScale(this->scale);

        this->sendEventChange(newEvent);

        // Don't erase user's text, but let user know the scale is unknown - how?
        //this->scaleNameEditor->setText({}, dontSendNotification);
    }
}

void KeySignatureDialog::onScaleNotePreview(int key)
{
    this->previewNote(this->rootKey + key);
}

//===----------------------------------------------------------------------===//
// TextEditor::Listener
//===----------------------------------------------------------------------===//

void KeySignatureDialog::textEditorTextChanged(TextEditor &ed)
{
    this->updateOkButtonState();
    this->scale = this->scale->withName(this->scaleNameEditor->getText());
    this->scaleEditor->setScale(this->scale);
    const auto newEvent = this->originalEvent
        .withRootKey(this->rootKey).withScale(scale);

    this->sendEventChange(newEvent);
}

void KeySignatureDialog::textEditorReturnKeyPressed(TextEditor &ed)
{
    this->textEditorFocusLost(ed);
}

void KeySignatureDialog::textEditorEscapeKeyPressed(TextEditor &)
{
    this->cancelAndDisappear();
}

void KeySignatureDialog::textEditorFocusLost(TextEditor &)
{
    this->updateOkButtonState();

    auto *focusedComponent = Component::getCurrentlyFocusedComponent();

    if (nullptr != dynamic_cast<TextEditor *>(focusedComponent) &&
        this->scaleNameEditor.get() != focusedComponent)
    {
        return; // other editor is focused
    }

    if (this->scaleNameEditor->getText().isNotEmpty() &&
        focusedComponent != this->okButton.get() &&
        focusedComponent != this->removeEventButton.get())
    {
        this->dismiss(); // apply on return key
    }
    else
    {
        this->scaleNameEditor->grabKeyboardFocus();
    }
}
