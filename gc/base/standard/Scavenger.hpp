/*******************************************************************************
 *
 * (c) Copyright IBM Corp. 1991, 2015
 *
 *  This program and the accompanying materials are made available
 *  under the terms of the Eclipse Public License v1.0 and
 *  Apache License v2.0 which accompanies this distribution.
 *
 *      The Eclipse Public License is available at
 *      http://www.eclipse.org/legal/epl-v10.html
 *
 *      The Apache License v2.0 is available at
 *      http://www.opensource.org/licenses/apache2.0.php
 *
 * Contributors:
 *    Multiple authors (IBM Corp.) - initial implementation and documentation
 *******************************************************************************/

#if !defined(SCAVENGER_HPP_)
#define SCAVENGER_HPP_

#include "omrcfg.h"
#include "modronopt.h"

#if defined(OMR_GC_MODRON_SCAVENGER)

#include "omrcomp.h"

#include "CollectionStatisticsStandard.hpp"
#include "Collector.hpp"
#include "CopyScanCacheList.hpp"
#include "CopyScanCacheStandard.hpp"
#include "CycleState.hpp"
#include "GCExtensionsBase.hpp"
 
struct J9HookInterface;
class GC_ObjectScanner;
class MM_AllocateDescription;
class MM_CollectorLanguageInterface;
class MM_Dispatcher;
class MM_EnvironmentBase;
class MM_HeapRegionManager;
class MM_MemoryPool;
class MM_MemorySubSpace;
class MM_MemorySubSpaceSemiSpace;
class MM_PhysicalSubArena;
class MM_RSOverflow;
class MM_SublistPool;

struct OMR_VM;

/**
 * @todo Provide class documentation
 * @ingroup GC_Modron_Standard
 */
class MM_Scavenger : public MM_Collector
{
	/*
	 * Data members
	 */
private:
	const uintptr_t _objectAlignmentInBytes;	/**< Run-time objects alignment in bytes */
	bool _isRememberedSetInOverflowAtTheBeginning; /**< Cached RS Overflow flag at the beginning of the scavenge */

	MM_GCExtensionsBase *_extensions;
	
	MM_Dispatcher *_dispatcher;

	volatile uintptr_t _doneIndex; /**< sequence ID of completeScan loop, which we may have a few during one GC cycle */

	MM_MemorySubSpaceSemiSpace *_activeSubSpace; /**< top level new subspace subject to GC */
	MM_MemorySubSpace *_evacuateMemorySubSpace; /**< cached pointer to evacuate subspace within active subspace */
	MM_MemorySubSpace *_survivorMemorySubSpace; /**< cached pointer to survivor subspace within active subspace */
	MM_MemorySubSpace *_tenureMemorySubSpace;

	void *_evacuateSpaceBase, *_evacuateSpaceTop;	/**< cached base and top heap pointers within evacuate subspace */
	void *_survivorSpaceBase, *_survivorSpaceTop;	/**< cached base and top heap pointers within survivor subspace */

	uintptr_t _tenureMask; /**< A bit mask indicating which generations should be tenured on scavenge. */
	bool _expandFailed;
	bool _failedTenureThresholdReached;
	uintptr_t _failedTenureLargestObject;
	uintptr_t _countSinceForcingGlobalGC;

	bool _expandTenureOnFailedAllocate;
	bool _cachedSemiSpaceResizableFlag;
	uintptr_t _minTenureFailureSize;
	uintptr_t _minSemiSpaceFailureSize;

	MM_CycleState _cycleState;  /**< Embedded cycle state to be used as the master cycle state for GC activity */
	MM_CollectionStatisticsStandard _collectionStatistics;  /** Common collect stats (memory, time etc.) */

	MM_CopyScanCacheList _scavengeCacheFreeList; /**< pool of unused copy-scan caches */
	MM_CopyScanCacheList _scavengeCacheScanList; /**< scan lists */
	volatile uintptr_t _cachedEntryCount; /**< non-empty scanCacheList count (not the total count of caches in the lists) */
	uintptr_t _cachesPerThread; /**< maximum number of copy and scan caches required per thread at any one time */
	omrthread_monitor_t _scanCacheMonitor; /**< monitor to synchronize threads on scan lists */
	omrthread_monitor_t _freeCacheMonitor; /**< monitor to synchronize threads on free list */
	volatile uintptr_t _waitingCount; /**< count of threads waiting  on scan cache queues (blocked via _scanCacheMonitor); threads never wait on _freeCacheMonitor */
	uintptr_t _cacheLineAlignment; /**< The number of bytes per cache line which is used to determine which boundaries in memory represent the beginning of a cache line */

	volatile bool _rescanThreadsForRememberedObjects; /**< VMDESIGN 2048: Indicates that thread-referenced objects were tenured and threads must be rescanned */
	bool _backOutFlag; /**< set to true if a thread is unable to copy an object due to lack of free space in both Survivor and Tenure */
	uintptr_t _backOutDoneIndex; /**< snapshot of _doneIndex, when backOut was detected */

	void *_heapBase;  /**< Cached base pointer of heap */
	void *_heapTop;  /**< Cached top pointer of heap */
	MM_HeapRegionManager *_regionManager;

protected:

public:
	OMR_VM *_omrVM;
	
	/*
	 * Function members
	 */
private:
	/**
	 * Hook callback. Called when a global collect has started
	 */
	static void hookGlobalCollectionStart(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData);

	/**
	 * Hook callback. Called when a global collect has completed
	 */
	static void hookGlobalCollectionComplete(J9HookInterface** hook, uintptr_t eventNum, void* eventData, void* userData);
	
	/**
	 *  This method is called on the start of a global GC.
	 *  @param env the current thread.
	 */
	void globalCollectionStart(MM_EnvironmentBase *env);
	
	/**
	 *  This method is called on the completion of a global GC.
	 *  @param env the current thread.
	 */
	void globalCollectionComplete(MM_EnvironmentBase *env);

	MMINLINE uintptr_t calculateOptimumCopyScanCacheSize(MM_EnvironmentStandard *env);
	MMINLINE MM_CopyScanCacheStandard *reserveMemoryForAllocateInSemiSpace(MM_EnvironmentStandard *env, omrobjectptr_t objectToEvacuate, uintptr_t objectReserveSizeInBytes);
	MM_CopyScanCacheStandard *reserveMemoryForAllocateInTenureSpace(MM_EnvironmentStandard *env, omrobjectptr_t objectToEvacuate, uintptr_t objectReserveSizeInBytes);

	/**
	 * Flush the threads reference and remembered set caches before waiting in getNextScanCache.
	 * This removes the requirement of a synchronization point after calls to completeScan when
	 * it is followed by reference or remembered set processing.
	 * @param env - current thread environment
	 */
	MMINLINE void flushBuffersForGetNextScanCache(MM_EnvironmentStandard *env);

	MM_CopyScanCacheStandard *getNextScanCache(MM_EnvironmentStandard *env);

	/**
	 * Implementation of CopyAndForward for slotObject input format
	 * @param slotObject input field in slotObject format
	 */
	MMINLINE bool copyAndForward(MM_EnvironmentStandard *env, GC_SlotObject *slotObject);

	MMINLINE bool copyAndForward(MM_EnvironmentStandard *env, volatile omrobjectptr_t *objectPtrIndirect);

	MMINLINE omrobjectptr_t copy(MM_EnvironmentStandard *env, MM_ForwardedHeader* forwardedHeader);

	MMINLINE void updateCopyScanCounts(MM_EnvironmentBase* env, uint64_t slotsScanned, uint64_t slotsCopied);
	bool splitIndexableObjectScanner(MM_EnvironmentStandard *env, GC_ObjectScanner *objectScanner, uintptr_t startIndex, omrobjectptr_t *rememberedSetSlot);

	/**
	 * Scavenges the contents of an object.
	 * @param env The environment.
	 * @param objectPtr The pointer to the object.
	 * @param scanCache The scan cache for the environment
	 * @param flags A bit map of GC_ObjectScanner::InstanceFlags.
	 * @return Whether or not objectPtr should be remembered.
	 */
	MMINLINE bool scavengeObjectSlots(MM_EnvironmentStandard *env, MM_CopyScanCacheStandard *scanCache, omrobjectptr_t objectPtr, uintptr_t flags, omrobjectptr_t *rememberedSetSlot);
	MMINLINE void incrementalScavengeObjectSlots(MM_EnvironmentStandard *env, omrobjectptr_t objectPtr, MM_CopyScanCacheStandard* scanCache, MM_CopyScanCacheStandard **nextScanCache);

	MMINLINE bool scavengeRememberedObject(MM_EnvironmentStandard *env, omrobjectptr_t objectPtr);
	void scavengeRememberedSetList(MM_EnvironmentStandard *env);
	void scavengeRememberedSetOverflow(MM_EnvironmentStandard *env);
	MMINLINE void flushRememberedSet(MM_EnvironmentStandard *env);
	void pruneRememberedSetList(MM_EnvironmentStandard *env);
	void pruneRememberedSetOverflow(MM_EnvironmentStandard *env);
	/**
	 * Checks if the  Object should be remembered or not
	 * @param env Standard Environment
	 * @param objectPtr The pointer to the  Object in Tenured Space.
	 * @return True If Object should be remembered
	 */
	bool walkObjectSlotsForRSO(MM_EnvironmentStandard *env, omrobjectptr_t objectPtr);

	/**
	 * BackOutFixSlot implementation
	 * @param slotObject input field in slotObject format
	 */
	bool backOutFixSlot(GC_SlotObject *slotObject);

	void backoutFixupAndReverseForwardPointersInSurvivor(MM_EnvironmentStandard *env);
	void completeBackOut(MM_EnvironmentStandard *env);

	/**
 	 * Request for percolate GC
 	 * 
 	 * @return true if Global GC was executed, false if concurrent kickoff forced or Global GC is not possible 
 	 */
	bool percolateGarbageCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, MM_AllocateDescription *allocDescription, PercolateReason percolateReason, uint32_t gcCode);
	
	void reportGCCycleStart(MM_EnvironmentStandard *env);
	void reportGCCycleEnd(MM_EnvironmentStandard *env);
	void reportGCCycleFinalIncrementEnding(MM_EnvironmentStandard *envModron);

	MMINLINE void clearExpandFailedFlag() { _expandFailed = false; };
	MMINLINE void setExpandFailedFlag() { _expandFailed = true; };
	MMINLINE bool expandFailed() { return _expandFailed; };

	MMINLINE void clearFailedTenureThresholdFlag() { _failedTenureThresholdReached = false; };
	MMINLINE void setFailedTenureThresholdFlag() { _failedTenureThresholdReached = true; };
	MMINLINE void setFailedTenureLargestObject(uintptr_t size) { _failedTenureLargestObject = size; };
	MMINLINE uintptr_t getFailedTenureLargestObject() { return _failedTenureLargestObject; };
	MMINLINE bool failedTenureThresholdReached() { return _failedTenureThresholdReached; };

	void completeScanCache(MM_EnvironmentStandard *env);
	void incrementalScanCacheBySlot(MM_EnvironmentStandard *env);

	MMINLINE MM_CopyScanCacheStandard *aliasToCopyCache(MM_EnvironmentStandard *env, GC_SlotObject *scannedSlot, MM_CopyScanCacheStandard* scanCache, MM_CopyScanCacheStandard* copyCache);
	MMINLINE uintptr_t scanCacheDistanceMetric(MM_CopyScanCacheStandard* cache, GC_SlotObject *scanSlot);
	MMINLINE uintptr_t copyCacheDistanceMetric(MM_CopyScanCacheStandard* cache);

	MMINLINE MM_CopyScanCacheStandard *getNextScanCacheFromList(MM_EnvironmentStandard *env);
	MMINLINE MM_CopyScanCacheStandard *getSurvivorCopyCache(MM_EnvironmentStandard *env);
	MMINLINE MM_CopyScanCacheStandard *getDeferredCopyCache(MM_EnvironmentStandard *env);

	void addCopyCachesToFreeList(MM_EnvironmentStandard *env);

	MMINLINE bool isWorkAvailableInCache(MM_CopyScanCacheStandard *scanCache);
	MMINLINE void addCacheEntryToScanListAndNotify(MM_EnvironmentStandard *env, MM_CopyScanCacheStandard *newCacheEntry);

	/**
	 * reinitializes the cache with the given base and top addresses.
	 * @param cache cache to be reinitialized.
	 * @param base base address of cache
	 * @param top top address of cache
	 */
	MMINLINE void reinitCache(MM_CopyScanCacheStandard *cache, void *base, void *top);

	/**
	 * An attempt to get a preallocated scan cache header, free list will be locked
	 * @param env - current thread environment
	 * @return pointer to scan cache header or NULL if attempt fail
	 */
	MMINLINE MM_CopyScanCacheStandard *getFreeCache(MM_EnvironmentStandard *env);

	/**
	 * An attempt to create chunk of scan cache headers in heap
	 * @param env - current thread environment
	 * @return pointer to allocated chunk of scan cache headers or NULL if attempt fail
	 */
	MM_CopyScanCacheStandard *createCacheInHeap(MM_EnvironmentStandard *env);

	/**
	 * Return cache back to free list if it is not used for copy.
	 * Clear cache if it has not been cleared yet
	 * @param env - current thread environment
	 * @param cache cache to be flushed
	 */
	void flushCache(MM_EnvironmentStandard *env, MM_CopyScanCacheStandard *cache);

	/**
	 * Check can local copy cache be reused
	 * To be reused cache should be has not been scanned and has no scan work to do
	 * @param env - current thread environment
	 * @param cache cache to be flushed
	 * @return true is cache can be reused
	 */
	bool canLocalCacheBeReused(MM_EnvironmentStandard *env, MM_CopyScanCacheStandard *cache);

	/**
	 * Release local Copy cache
	 * Put cache to scanned list if it has not been scanned and has scan work to do
	 * @param env - current thread environment
	 * @param cache cache to be flushed
	 * @return cache to reuse, if any
	 */
	MM_CopyScanCacheStandard *releaseLocalCopyCache(MM_EnvironmentStandard *env, MM_CopyScanCacheStandard *cache);

	/**
	 * Clear cache
	 * Return memory has not been allocated to memory pool
	 * @param env - current thread environment
	 * @param cache cache to be flushed
	 * @return true if sufficent amount of memory is left in the cache to create a 'remainder' for later usage
	 */
	bool clearCache(MM_EnvironmentStandard *env, MM_CopyScanCacheStandard *cache);

	/**
	 * Called (typically at the end of GC) to explicitly abandon the TLH remainders (for the calling thread)
	 */
	void abandonTLHRemainders(MM_EnvironmentStandard *env);
	void abandonSurvivorTLHRemainder(MM_EnvironmentStandard *env);
	void abandonTenureTLHRemainder(MM_EnvironmentStandard *env);

	void setBackOutFlag(MM_EnvironmentBase *env, bool value);
	MMINLINE bool backOutFlagRaised() { return _backOutFlag; }

	void reportGCStart(MM_EnvironmentStandard *env);
	void reportGCEnd(MM_EnvironmentStandard *env);
	void reportGCIncrementStart(MM_EnvironmentStandard *env);
	void reportGCIncrementEnd(MM_EnvironmentStandard *env);
	void reportScavengeStart(MM_EnvironmentStandard *env);
	void reportScavengeEnd(MM_EnvironmentStandard *env);

	MMINLINE MM_ScavengerHotFieldStats *getHotFieldStats(MM_EnvironmentStandard *env) { return &(env->_hotFieldStats); }
	void masterClearHotFieldStats();
	void masterReportHotFieldStats();
	void clearHotFieldStats(MM_EnvironmentStandard *env);
	void mergeHotFieldStats(MM_EnvironmentStandard *env);

	/**
	 * Add the specified object to the remembered set.
	 * Grow the remembered set if necessary and, if that fails, overflow.
	 * If the object is already remembered or is in new space, do nothing.
	 *
	 * @param env[in] the current thread
	 * @param objectPtr[in] the object to remember
	 */
	void rememberObject(MM_EnvironmentStandard *env, omrobjectptr_t objectPtr);

	/*
	 * Scan Tenure and add all found Remembered objects to Overflow
	 * @param env - Environment
	 * @param overflow - pointer to RS Overflow
	 */
	void addAllRememberedObjectsToOverflow(MM_EnvironmentStandard *env, MM_RSOverflow *overflow);

	void clearRememberedSetLists(MM_EnvironmentStandard *env);

	MMINLINE bool isRememberedSetInOverflowState() { return _extensions->isRememberedSetInOverflowState(); }
	MMINLINE void setRememberedSetOverflowState() { _extensions->setRememberedSetOverflowState(); }
	MMINLINE void clearRememberedSetOverflowState() { _extensions->clearRememberedSetOverflowState(); }

	/* VMDESIGN 2048: Auto-remember stack objects so JIT can omit generational barriers */
	void rescanThreadSlots(MM_EnvironmentStandard *env);

	/**
	 * Determine if the specified remembered object was referenced by a thread or stack.
	 * @see VMDESIGN 2048
	 *
	 * @param env[in] the current thread
	 * @param objectPtr[in] a remembered object
	 *
	 * @return true if the object is a remembered thread reference
	 */
	bool isRememberedThreadReference(MM_EnvironmentStandard *env, omrobjectptr_t objectPtr);

	/**
	 * Determine if the specified remembered object was referenced by a thread or stack and process it
	 *
	 * @param env[in] the current thread
	 * @param objectPtr[in] a remembered object
	 *
	 * @return true if the object is a remembered thread reference
	 */
	bool processRememberedThreadReference(MM_EnvironmentStandard *env, omrobjectptr_t objectPtr);

	void clearGCStats(MM_EnvironmentStandard *env);
	void mergeGCStats(MM_EnvironmentStandard *env);
	bool canCalcGCStats(MM_EnvironmentStandard *env);
	void calcGCStats(MM_EnvironmentStandard *env);

	void scavenge(MM_EnvironmentBase *env);
	bool scavengeCompletedSuccessfully(MM_EnvironmentStandard *env);
	void masterThreadGarbageCollect(MM_EnvironmentStandard *env);


	MMINLINE uintptr_t
	isTiltedScavenge()
	{
		return _extensions->tiltedScavenge ? 1 : 0;
	}

	/**
	 * Calculate tilt ratio
	 * @return tiltRatio
	 */
	uintptr_t calculateTiltRatio();

	/**
	 * The implementation of the Lookback scavenger tenure strategy.
	 * This strategy would, for each object age, check the survival history of
	 * that generation of objects (a diagonal down-left check in the survival
	 * history). If, historically, the survival rate of that generation of
	 * objects is always above minimumSurvivalRate, that age will be set for
	 * tenure this scavenge.
	 * @param minimumSurvivalRate The minimum survival rate required to consider tenuring.
	 * @return A tenure mask for the resulting ages to tenure.
	 */
	uintptr_t calculateTenureMaskUsingLookback(double minimumSurvivalRate);

	/**
	 * The implementation of the History scavenger tenure strategy.
	 * This strategy would, for each object age, check the survival history of
	 * that age (a vertical check in the survival history). If, historically,
	 * the survival rate of that age is always above minimumSurvivalRate, that
	 * age will be set for tenure this scavenge.
	 * @param minimumSurvivalRate The minimum survival rate required to consider tenuring.
	 * @return A tenure mask for the resulting ages to tenure.
	 */
	uintptr_t calculateTenureMaskUsingHistory(double minimumSurvivalRate);

	/**
	 * The implementation of the Fixed scavenger tenure strategy.
	 * This strategy will tenure any object who's age is above or equal to
	 * tenureAge.
	 * @param tenureAge The tenure age that objects should be tenured at.
	 * @return A tenure mask for the resulting ages to tenure.
	 */
	uintptr_t calculateTenureMaskUsingFixed(uintptr_t tenureAge);

	/**
	 * Calculates which generations should be tenured in the form of a bit mask.
	 * @return mask of ages to tenure
	 */
	uintptr_t calculateTenureMask();

	/**
	 * reset LargeAllocateStats in Tenure Space
	 * @param env Master GC thread.
	 */
	void resetTenureLargeAllocateStats(MM_EnvironmentBase *env);

protected:
	virtual void setupForGC(MM_EnvironmentBase *env);
	virtual void masterSetupForGC(MM_EnvironmentStandard *env);
	virtual void workerSetupForGC(MM_EnvironmentStandard *env);

	virtual bool initialize(MM_EnvironmentBase *env);
	virtual void tearDown(MM_EnvironmentBase *env);

	virtual bool internalGarbageCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, MM_AllocateDescription *allocDescription);
	virtual void internalPreCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, MM_AllocateDescription *allocDescription, uint32_t gcCode);
	virtual void internalPostCollect(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace);

	/**
	 * process LargeAllocateStats before GC
	 * merge largeObjectAllocateStats in nursery space(no averaging)
	 * @param env Master GC thread.
	 */
	virtual void processLargeAllocateStatsBeforeGC(MM_EnvironmentBase *env);

	/**
	 * process LargeAllocateStats after GC
	 * merge and average largeObjectAllocateStats in tenure space
	 * merge FreeEntry AllocateStats in tenure space
	 * estimate Fragmentation
	 * @param env Master GC thread.
	 */
	virtual void processLargeAllocateStatsAfterGC(MM_EnvironmentBase *env);
	
public:

	static MM_Scavenger *newInstance(MM_EnvironmentStandard *env, MM_CollectorLanguageInterface *cli, MM_HeapRegionManager *regionManager);
	virtual void kill(MM_EnvironmentBase *env);

	/**
	 * Determine whether the object pointer is found within the heap proper.
	 * @return Boolean indicating if the object pointer is within the heap boundaries.
	 */
	MMINLINE bool
	isHeapObject(omrobjectptr_t objectPtr)
	{
		return ((_heapBase <= (uint8_t *)objectPtr) && (_heapTop > (uint8_t *)objectPtr));
	}

	MMINLINE bool
	isObjectInNewSpace(omrobjectptr_t objectPtr)
	{
		return ((void *)objectPtr >= _survivorSpaceBase) && ((void *)objectPtr < _survivorSpaceTop);
	}

	MMINLINE bool
	isObjectInEvacuateMemory(omrobjectptr_t objectPtr)
	{
		/* check if the object in cached allocate (from GC perspective, evacuate) ranges */
		return ((void *)objectPtr >= _evacuateSpaceBase) && ((void *)objectPtr < _evacuateSpaceTop);
	}

	void workThreadGarbageCollect(MM_EnvironmentStandard *env);

	void scavengeRememberedSet(MM_EnvironmentStandard *env);

	void pruneRememberedSet(MM_EnvironmentStandard *env);

	virtual uintptr_t getVMStateID();

	bool completeScan(MM_EnvironmentStandard *env);

	/**
	 * Attempt to add the specified object to the current thread's remembered set fragment.
	 * Grow the remembered set if necessary and, if that fails, overflow.
	 * The object must already have its remembered bits set.
	 *
	 * @param env[in] the current thread
	 * @param objectPtr[in] the object to remember
	 */
	void addToRememberedSetFragment(MM_EnvironmentStandard *env, omrobjectptr_t objectPtr);

	/**
	 * Provide public (out-of-line) access to private (inline) copyAndForward(), copy() for client language
	 * runtime. Slot holding reference will be updated with new address for referent on return.
	 * @param[in] env Environment pointer for calling thread
	 * @param[in/out] slotPtr Pointer to slot holding reference to object to be copied and forwarded
	 */
	bool copyObjectSlot(MM_EnvironmentStandard *env, volatile omrobjectptr_t *slotPtr);
	bool copyObjectSlot(MM_EnvironmentStandard *env, GC_SlotObject* slotObject);
	omrobjectptr_t copyObject(MM_EnvironmentStandard *env, MM_ForwardedHeader* forwardedHeader);

	/**
	 * Update the given slot to point at the new location of the object, after copying
	 * the object if it was not already.
	 * Attempt to copy (either flip or tenure) the object and install a forwarding
	 * pointer at the new location. The object may have already been copied. In
	 * either case, update the slot to point at the new location of the object.
	 *
	 * @param env[in] the current thread
	 * @param objectPtrIndirect[in/out] the thread or stack slot to be scavenged
	 */
	void copyAndForwardThreadSlot(MM_EnvironmentStandard *env, omrobjectptr_t *objectPtrIndirect);

	/**
	 * This function is called at the end of scavenging if any stack- (or thread-) referenced
	 * objects were tenured during the scavenge. It is called by the RootScanner on each thread
	 * or stack slot.
	 *
	 * @param env[in] the current thread
	 * @param objectPtrIndirect[in] the slot to process
	 */
	void rescanThreadSlot(MM_EnvironmentStandard *env, omrobjectptr_t *objectPtrIndirect);

	uintptr_t getArraySplitAmount(MM_EnvironmentStandard *env, uintptr_t sizeInElements);

	void backOutObjectScan(MM_EnvironmentStandard *env, omrobjectptr_t objectPtr);
	/**
	 * BackOutFixSlotWithoutCompression implementation
	 * @param slotPrt input slot
	 */
	bool backOutFixSlotWithoutCompression(volatile omrobjectptr_t *slotPtr);
	virtual void *createSweepPoolState(MM_EnvironmentBase *env, MM_MemoryPool *memoryPool);
	virtual void deleteSweepPoolState(MM_EnvironmentBase *env, void *sweepPoolState);

	virtual bool heapAddRange(MM_EnvironmentBase *env, MM_MemorySubSpace *subspace, uintptr_t size, void *lowAddress, void *highAddress);
	virtual bool heapRemoveRange(MM_EnvironmentBase *env, MM_MemorySubSpace *subspace, uintptr_t size, void *lowAddress, void *highAddress, void *lowValidAddress, void *highValidAddress);

	virtual void collectorExpanded(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, uintptr_t expandSize);
	virtual bool canCollectorExpand(MM_EnvironmentBase *env, MM_MemorySubSpace *subSpace, uintptr_t expandSize);
	virtual uintptr_t getCollectorExpandSize(MM_EnvironmentBase *env);

	virtual void heapReconfigured(MM_EnvironmentBase *env);

	MM_Scavenger(MM_EnvironmentBase *env, MM_CollectorLanguageInterface *cli, MM_HeapRegionManager *regionManager) :
		MM_Collector(cli)
		, _objectAlignmentInBytes(env->getObjectAlignmentInBytes())
		, _isRememberedSetInOverflowAtTheBeginning(false)
		, _extensions(env->getExtensions())
		, _dispatcher(_extensions->dispatcher)
		, _doneIndex(0)
		, _activeSubSpace(NULL)
		, _evacuateMemorySubSpace(NULL)
		, _survivorMemorySubSpace(NULL)
		, _tenureMemorySubSpace(NULL)
		, _evacuateSpaceBase(NULL)
		, _evacuateSpaceTop(NULL)
		, _survivorSpaceBase(NULL)
		, _survivorSpaceTop(NULL)
		, _tenureMask(0)
		, _expandFailed(false)
		, _failedTenureThresholdReached(false)
		, _countSinceForcingGlobalGC(0)
		, _expandTenureOnFailedAllocate(true)
		, _minTenureFailureSize(UDATA_MAX)
		, _minSemiSpaceFailureSize(UDATA_MAX)
		, _cycleState()
		, _collectionStatistics()
		, _cachedEntryCount(0)
		, _cachesPerThread(0)
		, _scanCacheMonitor(NULL)
		, _freeCacheMonitor(NULL)
		, _waitingCount(0)
		, _cacheLineAlignment(0)
		, _rescanThreadsForRememberedObjects(false)
		, _backOutFlag(false)
		, _backOutDoneIndex(0)
		, _heapBase(NULL)
		, _heapTop(NULL)
		, _regionManager(regionManager)
		, _omrVM(env->getOmrVM())
	{
		_typeId = __FUNCTION__;
		_cycleType = OMR_GC_CYCLE_TYPE_SCAVENGE;
	}
};

#endif /* OMR_GC_MODRON_SCAVENGER */
#endif /* SCAVENGER_HPP_ */
