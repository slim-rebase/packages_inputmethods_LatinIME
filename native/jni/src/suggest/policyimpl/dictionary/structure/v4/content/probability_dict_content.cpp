/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "suggest/policyimpl/dictionary/structure/v4/content/probability_dict_content.h"

#include "suggest/policyimpl/dictionary/structure/v4/content/probability_entry.h"
#include "suggest/policyimpl/dictionary/structure/v4/content/terminal_position_lookup_table.h"
#include "suggest/policyimpl/dictionary/structure/v4/ver4_dict_constants.h"
#include "suggest/policyimpl/dictionary/utils/buffer_with_extendable_buffer.h"

namespace latinime {

const ProbabilityEntry ProbabilityDictContent::getProbabilityEntry(const int terminalId) const {
    if (terminalId < 0 || terminalId >= mSize) {
        // This method can be called with invalid terminal id during GC.
        return ProbabilityEntry(0 /* flags */, NOT_A_PROBABILITY);
    }
    const BufferWithExtendableBuffer *const buffer = getBuffer();
    int entryPos = getEntryPos(terminalId);
    const int flags = buffer->readUintAndAdvancePosition(
            Ver4DictConstants::FLAGS_IN_PROBABILITY_FILE_SIZE, &entryPos);
    const int probability = buffer->readUintAndAdvancePosition(
            Ver4DictConstants::PROBABILITY_SIZE, &entryPos);
    if (mHasHistoricalInfo) {
        const int timestamp = buffer->readUintAndAdvancePosition(
                Ver4DictConstants::TIME_STAMP_FIELD_SIZE, &entryPos);
        const int level = buffer->readUintAndAdvancePosition(
                Ver4DictConstants::WORD_LEVEL_FIELD_SIZE, &entryPos);
        const int count = buffer->readUintAndAdvancePosition(
                Ver4DictConstants::WORD_COUNT_FIELD_SIZE, &entryPos);
        const HistoricalInfo historicalInfo(timestamp, level, count);
        return ProbabilityEntry(flags, probability, &historicalInfo);
    } else {
        return ProbabilityEntry(flags, probability);
    }
}

bool ProbabilityDictContent::setProbabilityEntry(const int terminalId,
        const ProbabilityEntry *const probabilityEntry) {
    if (terminalId < 0) {
        return false;
    }
    const int entryPos = getEntryPos(terminalId);
    if (terminalId >= mSize) {
        ProbabilityEntry dummyEntry;
        // Write new entry.
        int writingPos = getBuffer()->getTailPosition();
        while (writingPos <= entryPos) {
            // Fulfilling with dummy entries until writingPos.
            if (!writeEntry(&dummyEntry, writingPos)) {
                AKLOGE("Cannot write dummy entry. pos: %d, mSize: %d", writingPos, mSize);
                return false;
            }
            writingPos += getEntrySize();
            mSize++;
        }
    }
    return writeEntry(probabilityEntry, entryPos);
}

bool ProbabilityDictContent::flushToFile(const char *const dictPath) const {
    if (getEntryPos(mSize) < getBuffer()->getTailPosition()) {
        ProbabilityDictContent probabilityDictContentToWrite(mHasHistoricalInfo);
        for (int i = 0; i < mSize; ++i) {
            const ProbabilityEntry probabilityEntry = getProbabilityEntry(i);
            if (!probabilityDictContentToWrite.setProbabilityEntry(i, &probabilityEntry)) {
                AKLOGE("Cannot set probability entry in flushToFile. terminalId: %d", i);
                return false;
            }
        }
        return probabilityDictContentToWrite.flush(dictPath,
                Ver4DictConstants::FREQ_FILE_EXTENSION);
    } else {
        return flush(dictPath, Ver4DictConstants::FREQ_FILE_EXTENSION);
    }
}

bool ProbabilityDictContent::runGC(
        const TerminalPositionLookupTable::TerminalIdMap *const terminalIdMap,
        const ProbabilityDictContent *const originalProbabilityDictContent) {
    mSize = 0;
    for (TerminalPositionLookupTable::TerminalIdMap::const_iterator it = terminalIdMap->begin();
            it != terminalIdMap->end(); ++it) {
        const ProbabilityEntry probabilityEntry =
                originalProbabilityDictContent->getProbabilityEntry(it->first);
        if (!setProbabilityEntry(it->second, &probabilityEntry)) {
            AKLOGE("Cannot set probability entry in runGC. terminalId: %d", it->second);
            return false;
        }
        mSize++;
    }
    return true;
}

int ProbabilityDictContent::getEntrySize() const {
    if (mHasHistoricalInfo) {
        return Ver4DictConstants::FLAGS_IN_PROBABILITY_FILE_SIZE
                + Ver4DictConstants::PROBABILITY_SIZE
                + Ver4DictConstants::TIME_STAMP_FIELD_SIZE
                + Ver4DictConstants::WORD_LEVEL_FIELD_SIZE
                + Ver4DictConstants::WORD_COUNT_FIELD_SIZE;
    } else {
        return Ver4DictConstants::FLAGS_IN_PROBABILITY_FILE_SIZE
                + Ver4DictConstants::PROBABILITY_SIZE;
    }
}

int ProbabilityDictContent::getEntryPos(const int terminalId) const {
    return terminalId * getEntrySize();
}

bool ProbabilityDictContent::writeEntry(const ProbabilityEntry *const probabilityEntry,
        const int entryPos) {
    BufferWithExtendableBuffer *const bufferToWrite = getWritableBuffer();
    int writingPos = entryPos;
    if (!bufferToWrite->writeUintAndAdvancePosition(probabilityEntry->getFlags(),
            Ver4DictConstants::FLAGS_IN_PROBABILITY_FILE_SIZE, &writingPos)) {
        AKLOGE("Cannot write flags in probability dict content. pos: %d", writingPos);
        return false;
    }
    if (!bufferToWrite->writeUintAndAdvancePosition(probabilityEntry->getProbability(),
            Ver4DictConstants::PROBABILITY_SIZE, &writingPos)) {
        AKLOGE("Cannot write probability in probability dict content. pos: %d", writingPos);
        return false;
    }
    if (mHasHistoricalInfo) {
        const HistoricalInfo *const historicalInfo = probabilityEntry->getHistoricalInfo();
        if (!bufferToWrite->writeUintAndAdvancePosition(historicalInfo->getTimeStamp(),
                Ver4DictConstants::TIME_STAMP_FIELD_SIZE, &writingPos)) {
            AKLOGE("Cannot write timestamp in probability dict content. pos: %d", writingPos);
            return false;
        }
        if (!bufferToWrite->writeUintAndAdvancePosition(historicalInfo->getLevel(),
                Ver4DictConstants::WORD_LEVEL_FIELD_SIZE, &writingPos)) {
            AKLOGE("Cannot write level in probability dict content. pos: %d", writingPos);
            return false;
        }
        if (!bufferToWrite->writeUintAndAdvancePosition(historicalInfo->getCount(),
                Ver4DictConstants::WORD_COUNT_FIELD_SIZE, &writingPos)) {
            AKLOGE("Cannot write count in probability dict content. pos: %d", writingPos);
            return false;
        }
    }
    return true;
}

} // namespace latinime
