#include "RowStoreScanProcessor.hpp"


namespace tell {
namespace store {
namespace deltamain {


RowStoreScanProcessor::RowStoreScanProcessor(const std::shared_ptr<crossbow::allocator>& alloc,
        const PageList* pages,
        size_t pageIdx,
        size_t pageEndIdx,
        const LogIterator& logIter,
        const LogIterator& logEnd,
        PageManager* pageManager,
        const char* queryBuffer,
        const std::vector<ScanQuery*>& queryData,
        const Record* record)
    : mAllocator(alloc)
    , pages(pages)
    , pageIdx(pageIdx)
    , pageEndIdx(pageEndIdx)
    , logIter(logIter)
    , logEnd(logEnd)
    , pageManager(pageManager)
    , query(queryBuffer, queryData)
    , record(record)
    , pageIter(RowStorePage(*pageManager, (*pages)[pageIdx]).begin())
    , pageEnd (RowStorePage(*pageManager, (*pages)[pageIdx]).end())
    , currKey(0u)
{
}

void RowStoreScanProcessor::process()
{
    for (setCurrentEntry(); currVersionIter.isValid(); next()) {
        query.processRecord(*currVersionIter->record(), currKey, currVersionIter->data(), currVersionIter->size(),
                currVersionIter->validFrom(), currVersionIter->validTo());
    }
}

void RowStoreScanProcessor::next()
{
    // This assures that the iterator is invalid when we reached the end
    if (currVersionIter.isValid() && (++currVersionIter).isValid()) {
        return;
    }
    if (logIter != logEnd) {
        ++logIter;
    } else if (pageIter != pageEnd) {
        ++pageIter;
    } else {
        ++pageIdx;
        if (pageIdx >= pageEndIdx)
            return;
        RowStorePage p(*pageManager, (*pages)[pageIdx]);
        pageIter = p.begin();
        pageEnd = p.end();
    }
    setCurrentEntry();
}

void RowStoreScanProcessor::setCurrentEntry()
{
    while (logIter != logEnd) {
        if (logIter->sealed()) {
            CDMRecord rec(logIter->data());
            if (rec.isValidDataRecord()) {
                currKey = rec.key();
                currVersionIter = rec.getVersionIterator(record);
                return;
            }
        }
        ++logIter;
    }
    if (pageIdx >= pageEndIdx)
        return;
    while (true) {
        while (pageIter != pageEnd) {
            CDMRecord rec(*pageIter);
            if (rec.isValidDataRecord()) {
                currKey = rec.key();
                currVersionIter = rec.getVersionIterator(record);
                return;
            }
            ++pageIter;
        }
        ++pageIdx;
        if (pageIdx >= pageEndIdx)
            return;
        RowStorePage p(*pageManager, (*pages)[pageIdx]);
        pageIter = p.begin();
        pageEnd = p.end();
    }
}

} // namespace deltamain
} // namespace store
} // namespace tell