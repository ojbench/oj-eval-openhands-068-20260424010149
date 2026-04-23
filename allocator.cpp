


#include "allocator.hpp"
#include <cstring>
#include <algorithm>

// Constructor
TLSFAllocator::TLSFAllocator(std::size_t memoryPoolSize) : memoryPool(nullptr), poolSize(0) {
    initializeMemoryPool(memoryPoolSize);
}

// Destructor
TLSFAllocator::~TLSFAllocator() {
    if (memoryPool) {
        std::free(memoryPool);
        memoryPool = nullptr;
    }
}

// Initialize the memory pool
void TLSFAllocator::initializeMemoryPool(std::size_t size) {
    // Allocate memory for the pool
    memoryPool = std::malloc(size);
    if (!memoryPool) {
        return;
    }
    
    poolSize = size;
    
    // Initialize index
    index.fliBitmap = 0;
    for (int i = 0; i < FLI_SIZE; ++i) {
        index.sliBitmaps[i] = 0;
        for (int j = 0; j < SLI_SIZE; ++j) {
            index.freeLists[i][j] = nullptr;
        }
    }
    
    // Create a single free block that covers the entire pool
    BlockHeader* header = static_cast<BlockHeader*>(memoryPool);
    header->data = static_cast<char*>(memoryPool) + sizeof(BlockHeader);
    header->size = size;
    header->isFree = true;
    header->prevPhysBlock = nullptr;
    header->nextPhysBlock = nullptr;
    
    FreeBlock* freeBlock = static_cast<FreeBlock*>(header);
    freeBlock->prevFree = nullptr;
    freeBlock->nextFree = nullptr;
    
    // Insert the free block into the index
    insertFreeBlock(freeBlock);
}

// Allocate memory
void* TLSFAllocator::allocate(std::size_t size) {
    if (size == 0) {
        return nullptr;
    }
    
    // Add space for header
    std::size_t blockSize = size + sizeof(BlockHeader);
    
    // Find a suitable block
    FreeBlock* block = findSuitableBlock(blockSize);
    if (!block) {
        return nullptr; // No suitable block found
    }
    
    // Remove the block from the free list
    removeFreeBlock(block);
    
    // Split the block if it's too large
    if (block->size > blockSize + sizeof(BlockHeader) + 16) { // Minimum block size for splitting
        splitBlock(block, blockSize);
    }
    
    // Mark as allocated
    block->isFree = false;
    
    // Return pointer to data
    return block->data;
}

// Deallocate memory
void TLSFAllocator::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }
    
    // Get the block header from the data pointer
    BlockHeader* header = static_cast<BlockHeader*>(
        static_cast<char*>(ptr) - sizeof(BlockHeader)
    );
    
    // Mark as free
    header->isFree = true;
    
    // Convert to FreeBlock
    FreeBlock* freeBlock = static_cast<FreeBlock*>(header);
    
    // Merge with adjacent free blocks
    mergeAdjacentFreeBlocks(freeBlock);
    
    // Insert back into the free list
    insertFreeBlock(freeBlock);
}

// Get memory pool start
void* TLSFAllocator::getMemoryPoolStart() const {
    return memoryPool;
}

// Get memory pool size
std::size_t TLSFAllocator::getMemoryPoolSize() const {
    return poolSize;
}

// Get maximum available block size
std::size_t TLSFAllocator::getMaxAvailableBlockSize() const {
    std::size_t maxSize = 0;
    
    // Iterate through all free lists to find the largest block
    for (int fli = 0; fli < FLI_SIZE; ++fli) {
        if (index.fliBitmap & (1U << fli)) {
            for (int sli = 0; sli < SLI_SIZE; ++sli) {
                if (index.sliBitmaps[fli] & (1U << sli)) {
                    FreeBlock* block = index.freeLists[fli][sli];
                    while (block) {
                        if (block->size > maxSize) {
                            maxSize = block->size;
                        }
                        block = block->nextFree;
                    }
                }
            }
        }
    }
    
    return maxSize;
}

// Split a block into two
void TLSFAllocator::splitBlock(FreeBlock* block, std::size_t size) {
    // Calculate the size of the remaining block
    std::size_t remainingSize = block->size - size;
    
    // Create a new block header for the remaining space
    BlockHeader* newHeader = static_cast<BlockHeader*>(
        static_cast<char*>(block) + size
    );
    
    // Set up the new block
    newHeader->data = static_cast<char*>(newHeader) + sizeof(BlockHeader);
    newHeader->size = remainingSize;
    newHeader->isFree = true;
    newHeader->prevPhysBlock = block;
    newHeader->nextPhysBlock = block->nextPhysBlock;
    
    // Update physical links
    if (block->nextPhysBlock) {
        block->nextPhysBlock->prevPhysBlock = newHeader;
    }
    
    // Convert to FreeBlock
    FreeBlock* newFreeBlock = static_cast<FreeBlock*>(newHeader);
    newFreeBlock->prevFree = nullptr;
    newFreeBlock->nextFree = nullptr;
    
    // Update the original block size
    block->size = size;
    block->nextPhysBlock = newHeader;
    
    // Insert the new block into the free list
    insertFreeBlock(newFreeBlock);
}

// Merge adjacent free blocks
void TLSFAllocator::mergeAdjacentFreeBlocks(FreeBlock* block) {
    // Check if the next physical block is free and merge
    if (block->nextPhysBlock && block->nextPhysBlock->isFree) {
        FreeBlock* nextBlock = static_cast<FreeBlock*>(block->nextPhysBlock);
        
        // Remove next block from free list
        removeFreeBlock(nextBlock);
        
        // Merge blocks
        block->size += nextBlock->size;
        block->nextPhysBlock = nextBlock->nextPhysBlock;
        
        // Update physical links
        if (nextBlock->nextPhysBlock) {
            nextBlock->nextPhysBlock->prevPhysBlock = block;
        }
    }
    
    // Check if the previous physical block is free and merge
    if (block->prevPhysBlock && block->prevPhysBlock->isFree) {
        FreeBlock* prevBlock = static_cast<FreeBlock*>(block->prevPhysBlock);
        
        // Remove current block from free list
        removeFreeBlock(block);
        
        // Remove previous block from free list
        removeFreeBlock(prevBlock);
        
        // Merge blocks
        prevBlock->size += block->size;
        prevBlock->nextPhysBlock = block->nextPhysBlock;
        
        // Update physical links
        if (block->nextPhysBlock) {
            block->nextPhysBlock->prevPhysBlock = prevBlock;
        }
        
        // Use the previous block as the merged block
        block = prevBlock;
    }
}

// Insert a free block into the index
void TLSFAllocator::insertFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    // Insert at the beginning of the list
    FreeBlock* head = index.freeLists[fli][sli];
    block->nextFree = head;
    block->prevFree = nullptr;
    
    if (head) {
        head->prevFree = block;
    }
    
    index.freeLists[fli][sli] = block;
    
    // Update bitmaps
    index.fliBitmap |= (1U << fli);
    index.sliBitmaps[fli] |= (1U << sli);
}

// Remove a free block from the index
void TLSFAllocator::removeFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    // Update links
    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        // This block is the head
        index.freeLists[fli][sli] = block->nextFree;
    }
    
    if (block->nextFree) {
        block->nextFree->prevFree = block->prevFree;
    }
    
    // Update bitmaps
    if (!index.freeLists[fli][sli]) {
        index.sliBitmaps[fli] &= ~(1U << sli);
        if (!index.sliBitmaps[fli]) {
            index.fliBitmap &= ~(1U << fli);
        }
    }
}

// Find a suitable block for allocation
FreeBlock* TLSFAllocator::findSuitableBlock(std::size_t size) {
    int fli, sli;
    mappingFunction(size, fli, sli);
    
    // First, try to find a block in the exact FLI/SLI
    if (index.fliBitmap & (1U << fli)) {
        if (index.sliBitmaps[fli] & (1U << sli)) {
            return index.freeLists[fli][sli];
        }
    }
    
    // If not found, look for a larger block in the same FLI
    std::uint16_t sliMask = index.sliBitmaps[fli] & (~((1U << sli) - 1));
    if (sliMask) {
        int nextSli = __builtin_ctz(sliMask); // Count trailing zeros
        return index.freeLists[fli][nextSli];
    }
    
    // If not found, look for a block in a higher FLI
    std::uint32_t fliMask = index.fliBitmap & (~((1U << fli) - 1));
    if (fliMask) {
        int nextFli = __builtin_ctz(fliMask); // Count trailing zeros
        int nextSli = __builtin_ctz(index.sliBitmaps[nextFli]); // Find first set bit
        return index.freeLists[nextFli][nextSli];
    }
    
    // No suitable block found
    return nullptr;
}

// Map size to FLI and SLI
void TLSFAllocator::mappingFunction(std::size_t size, int& fli, int& sli) {
    if (size < 128) {
        // For small sizes, use a linear mapping
        fli = 0;
        sli = static_cast<int>(size) - 1;
    } else {
        // Calculate FLI using log2
        fli = std::min(31, static_cast<int>(31 - __builtin_clz(static_cast<unsigned int>(size))));
        
        // Calculate divisions based on hint: divisions = min((1 << FLI), 16)
        int divisions = std::min(1 << fli, 16);
        
        // Calculate SLI
        std::size_t base = 1U << fli;
        sli = static_cast<int>(((size - base) * divisions) / base);
        sli = std::min(sli, divisions - 1);
    }
}


