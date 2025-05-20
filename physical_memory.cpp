#include "physical_memory.h"
#include "common.h"

MemoryPo2CTable::Bin::Bin() {
    for (int i = 0; i < MemoryPo2CTable::kBinSize; ++i)
        bin_[i] = i + 2;
}

bool MemoryPo2CTable::Bin::Full() const {
    return cnt_ == MemoryPo2CTable::kBinSize;
}

UINT8 MemoryPo2CTable::Bin::Count() const {
    return cnt_;
}

UINT8 MemoryPo2CTable::Bin::Insert(UINT64 _, UINT8 keyWidth) {
    if (this->Full())
        return MemoryPo2CTable::kOverflowTinyPtr;
    assert(keyWidth <= 8);
    UINT8 tmp = head_;
    const UINT8 threadholdIndex = (1 << (keyWidth - 1));
    if (keyWidth != 8 && tmp >= threadholdIndex) {
        // this means the tinyptr can only pointed to the first 2^(keyWidth - 1) frames
        // in this case, we start with head_ and traverse the free list until we find a free frame
        // that is in the range of the keyWidth
        UINT8 prevBinIndex = head_;
        while (tmp < threadholdIndex) {
            if (bin_[tmp - 1] == head_) {
                // this means there's no free frame in the range of keyWidth
                // so the insert is not possible
                assert(0);
            }
            prevBinIndex = tmp;
            tmp = bin_[tmp - 1];
        }
        bin_[prevBinIndex - 1] = bin_[tmp - 1];

    } else {

        head_ = bin_[head_ - 1];
    }
    cnt_++;
    assert(head_ <= (1 << 7));
    assert(head_ != 0);

    return tmp;
}

bool MemoryPo2CTable::Bin::Update(UINT64 key, UINT8 ptr, UINT64 value) {
    assert(ptr);
    assert(ptr < (1 << 7));
    --ptr;

    return 1;
}

bool MemoryPo2CTable::Bin::Free(UINT64 key, UINT8 ptr) {
    assert(ptr);
    assert(ptr < (1 << 7));
    --ptr;

    bin_[ptr] = head_;
    head_ = ptr + 1;
    assert(head_ <= (1 << 7));
    assert(head_ != 0);
    cnt_--;
    return 1;
}
