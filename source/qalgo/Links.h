#ifndef QFUSION_LINKS_H
#define QFUSION_LINKS_H

#include <assert.h>

// TODO: Lift also utilities that operate on int16_t offsets instead of pointers here

/**
 * Links an item that has an intrusive array of links to a list head.
 * The item becomes the list head, the former head is linked to the "next" link
 * and the "prev" link is nullified (that's the list head contract).
 * @tparam Item any type that has two accessible arrays of links:
 * {@code Item *prev[]} and {@code Item *next[]}.
 * Arrays of pointers are used instead of single pointers
 * so an item can be linked to multiple lists simultaneously.
 * Consequently these arrays should have a size corresponding to
 * a desired number of lists an item can be linked to.
 * @param item an item to link
 * @param listHeadRef an address of the list head which is itself a pointer
 * @param listIndex an index of list to link to
 * @return the newly linked item (same as the argument) conforming to fluent API style.
 */
template<typename Item>
static inline Item *Link( Item *item, Item **listHeadRef, int linksIndex ) {
	if( *listHeadRef ) {
		( *listHeadRef )->prev[linksIndex] = item;
	}
	item->prev[linksIndex] = nullptr;
	item->next[linksIndex] = *listHeadRef;
	*listHeadRef = item;
	return item;
}

/**
 * Unlinks an item that has an intrusive array of links from a list head.
 * Modifies the list head as well if the item was the head.
 * @tparam Item any type that has two accessible arrays of links:
 * {@code Item *prev[]} and {@code Item *next[]}.
 * Arrays of pointers are used instead of single pointers
 * so an item can be linked to multiple lists.
 * Consequently these arrays should have a size corresponding to a
 * desired number of lists an item can be linked to simultaneously.
 * @param item an item to unlink
 * @param listHeadRef an address of the list head which is itself a pointer
 * @param listIndex an index of list to unlink from.
 * @return the newly unlinked item (same as the argument) conforming to fluent API style.
 */
template<typename Item>
static inline Item *Unlink( Item *item, Item **listHeadRef, int listIndex ) {
	if( auto *next = item->next[listIndex] ) {
		next->prev[listIndex] = item->prev[listIndex];
	}
	if( auto *prev = item->prev[listIndex] ) {
		prev->next[listIndex] = item->next[listIndex];
	} else {
		assert( item == *listHeadRef );
		*listHeadRef = item->next[listIndex];
	}

	item->prev[listIndex] = nullptr;
	item->next[listIndex] = nullptr;
	return item;
}

#endif
