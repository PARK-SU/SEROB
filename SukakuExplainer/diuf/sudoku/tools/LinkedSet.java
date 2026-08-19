/*
 * Project: Sudoku Explainer
 * Copyright (C) 2006-2007 Nicolas Juillerat
 * Available under the terms of the Lesser General Public License (LGPL)
 * Modified for the SEROB WebAssembly rating engine on 2026-08-20.
 * SPDX-License-Identifier: LGPL-2.1-only
 */
package diuf.sudoku.tools;

import java.util.*;

/**
 * Reflective set implementation. Like a <code>LinkedHashSet</code>
 * but with a weird method:
 * {@link #get(Object)}, which returns the element
 * of the set that is equal to the given element.
 * This is especially usefull when the implementation
 * of <code>equals</code> does not compare all fields.
 */
public class LinkedSet<T> extends AbstractSet<T> {

    /*
     * Chaining is the only user of this class. Its Potential keys have a
     * collision-free hash in the range 0..1459, so a direct index avoids a
     * LinkedHashMap allocation for every implication search. The insertion
     * list preserves the old map's iteration order and key-replacement
     * behaviour: an equal add updates get(), but does not replace the key
     * returned by the iterator.
     */
    private final Object[] target = new Object[1460];
    private final ArrayList<T> order = new ArrayList<T>();

    private int index(Object o) {
        int result = o.hashCode();
        if (result < 0 || result >= target.length)
            throw new IllegalArgumentException("LinkedSet key hash is out of range");
        return result;
    }


    @Override
    public boolean add(T o) {
        int index = index(o);
        Object previous = target[index];
        if (previous == null)
            order.add(o);
        else if (!previous.equals(o))
            throw new IllegalArgumentException("LinkedSet key hash collision");
        target[index] = o;
        return previous != null;
    }

    @Override
    public void clear() {
        for (T value : order)
            target[index(value)] = null;
        order.clear();
    }

    @Override
    public boolean contains(Object o) {
        Object value = target[index(o)];
        return value != null && value.equals(o);
    }

    @SuppressWarnings("unchecked")
    public T get(T o) {
        Object value = target[index(o)];
        return value != null && value.equals(o) ? (T)value : null;
    }

    @Override
    public Iterator<T> iterator() {
        final Iterator<T> iterator = order.iterator();
        return new Iterator<T>() {
            private T current;

            public boolean hasNext() {
                return iterator.hasNext();
            }

            public T next() {
                current = iterator.next();
                return current;
            }

            public void remove() {
                if (current == null)
                    throw new IllegalStateException();
                target[index(current)] = null;
                iterator.remove();
                current = null;
            }
        };
    }

    @Override
    public boolean remove(Object o) {
        if (!contains(o))
            return false;
        for (Iterator<T> iterator = iterator(); iterator.hasNext();) {
            if (iterator.next().equals(o)) {
                iterator.remove();
                return true;
            }
        }
        return false;
    }

    @Override
    public int size() {
        return order.size();
    }
    
    @Override
    public int hashCode() {
        int ret = 0;
        for (T value : order)
            ret ^= value.hashCode();
        return ret;
    }
}
