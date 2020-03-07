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
#include "Playhead.h"
#include "Transport.h"
#include "HybridRoll.h"
#include "ColourIDs.h"
#include "PlayerThread.h"

#define PLAYHEAD_PADDING 2

//#define PLAYHEAD_UPDATE_TIME_MS (1000 / 50)
#define PLAYHEAD_UPDATE_TIME_MS 7

Playhead::Playhead(HybridRoll &parentRoll,
    Transport &owner,
    Playhead::Listener *movementListener /*= nullptr*/,
    int width /*= 2*/) :
    roll(parentRoll),
    transport(owner),
    playheadWidth(width + PLAYHEAD_PADDING),
    listener(movementListener),
    shadeColour(findDefaultColour(ColourIDs::Roll::playheadShade)),
    playbackColour(findDefaultColour(ColourIDs::Roll::playheadPlayback)),
    recordingColour(findDefaultColour(ColourIDs::Roll::playheadRecording))
{
    this->currentColour = this->playbackColour;

    this->setInterceptsMouseClicks(false, false);
    this->setPaintingIsUnclipped(true);
    this->setAlwaysOnTop(true);
    this->setSize(this->playheadWidth, 1);

    this->lastCorrectPosition = this->transport.getSeekBeat();
    this->timerStartPosition = this->lastCorrectPosition;
    this->timerStartTime = Time::getMillisecondCounterHiRes();

    this->transport.addTransportListener(this);
}

Playhead::~Playhead()
{
    this->transport.removeTransportListener(this);
}


//===----------------------------------------------------------------------===//
// TransportListener
//===----------------------------------------------------------------------===//

void Playhead::onSeek(float beatPosition, double currentTimeMs, double totalTimeMs)
{
    this->lastCorrectPosition = beatPosition;

    this->triggerAsyncUpdate();

    if (this->isTimerRunning())
    {
        this->timerStartTime = Time::getMillisecondCounterHiRes();
        this->timerStartPosition = this->lastCorrectPosition;
        //this->startTimer(PLAYHEAD_UPDATE_TIME_MS);
    }
}

void Playhead::onTempoChanged(double msPerQuarter)
{
    this->msPerQuarterNote = jmax(msPerQuarter, 0.01);
        
    if (this->isTimerRunning())
    {
        this->timerStartTime = Time::getMillisecondCounterHiRes();
        this->timerStartPosition = this->lastCorrectPosition;
    }
}

void Playhead::onPlay()
{
    this->timerStartTime = Time::getMillisecondCounterHiRes();
    this->timerStartPosition = this->lastCorrectPosition;

    this->startTimer(PLAYHEAD_UPDATE_TIME_MS);
}

void Playhead::onRecord()
{
    this->currentColour = this->recordingColour;
    this->repaint();
}

void Playhead::onStop()
{
    this->currentColour = this->playbackColour;
    this->repaint();

    this->stopTimer();

    this->timerStartTime = 0.0;
    this->timerStartPosition = 0.0;
}


//===----------------------------------------------------------------------===//
// Timer
//===----------------------------------------------------------------------===//

void Playhead::timerCallback()
{
    this->triggerAsyncUpdate();
}


//===----------------------------------------------------------------------===//
// AsyncUpdater
//===----------------------------------------------------------------------===//

void Playhead::handleAsyncUpdate()
{
    if (this->isTimerRunning())
    {
        this->tick();
    }
    else
    {
        this->updatePosition(this->lastCorrectPosition.get());
    }
}


//===----------------------------------------------------------------------===//
// Component
//===----------------------------------------------------------------------===//

void Playhead::paint(Graphics &g)
{
    g.setColour(this->currentColour);
    g.fillRect(0, 0, 1, this->getHeight());

    g.setColour(this->shadeColour);
    g.fillRect(1, 0, 1, this->getHeight());
}

void Playhead::parentSizeChanged()
{
    this->parentChanged();
}

void Playhead::parentHierarchyChanged()
{
    this->parentChanged();
}

void Playhead::parentChanged()
{
    if (this->getParentComponent() != nullptr)
    {
        this->setSize(this->playheadWidth, this->getParentHeight());
        
        if (this->isTimerRunning())
        {
            this->tick();
        }
        else
        {
            this->updatePosition(this->lastCorrectPosition.get());
            this->toFront(false);
        }
    }
}

void Playhead::updatePosition(double position)
{
    // fixme: make smooth movement which depends on parent width
    const int &newX = this->roll.getPlayheadPositionByBeat(position, float(this->getParentWidth()));
    this->setTopLeftPosition(newX, 0);

    if (this->listener != nullptr)
    {
        this->listener->onPlayheadMoved(newX);
    }
}

void Playhead::tick()
{
    // fixme smooth movement
    const double timeOffsetMs = Time::getMillisecondCounterHiRes() - this->timerStartTime.get();
    const double positionOffset = (timeOffsetMs / this->transport.getTotalTime()) / this->msPerQuarterNote.get();
    const double estimatedPosition = this->timerStartPosition.get() + positionOffset;
    this->updatePosition(estimatedPosition);
}
