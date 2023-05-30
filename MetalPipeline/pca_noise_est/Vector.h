#ifndef VECTOR_H_
#define VECTOR_H_

//=============================================================================

#include "Param.h"

//=============================================================================

template <unsigned Dim>
struct VectorT {
    double data[Dim];

    operator const double*() const { return data; }
    operator double*() { return data; }

    VectorT& operator+=(const VectorT& a);
};

//=============================================================================

template <unsigned Dim>
inline VectorT<Dim>& VectorT<Dim>::operator+=(const VectorT& a) {
    for (unsigned i = 0; i < Dim; ++i) data[i] += a.data[i];
    return *this;
};

//=============================================================================

typedef VectorT<M> Vector;
typedef VectorT<M * M> Matrix;

#endif /*VECTOR_H_*/
