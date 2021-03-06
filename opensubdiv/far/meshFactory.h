//
//   Copyright 2013 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#ifndef FAR_MESH_FACTORY_H
#define FAR_MESH_FACTORY_H

#include "../version.h"

// Activate Hbr feature adaptive tagging : in order to process the HbrMesh
// adaptively, some tag data is added to HbrFace, HbrVertex and HbrHalfedge.
// While small, these tags incur some performance costs and are by default
// disabled.
#ifndef HBR_ADAPTIVE
#define HBR_ADAPTIVE
#endif

#include "../hbr/mesh.h"
#include "../hbr/bilinear.h"
#include "../hbr/catmark.h"
#include "../hbr/loop.h"

#include "../far/mesh.h"
#include "../far/dispatcher.h"
#include "../far/bilinearSubdivisionTablesFactory.h"
#include "../far/catmarkSubdivisionTablesFactory.h"
#include "../far/loopSubdivisionTablesFactory.h"
#include "../far/patchTables.h"
#include "../far/patchTablesFactory.h"
#include "../far/vertexEditTablesFactory.h"

#include <typeinfo>
#include <set>

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

/// \brief Instantiates a FarMesh from an HbrMesh.
///
/// FarMeshFactory requires a 2 steps process :
/// 1. Instantiate a FarMeshFactory object from an HbrMesh
/// 2. Call "Create" to obtain the FarMesh instance
///
/// This tiered factory approach offers client-code the opportunity to access
/// useful transient information tied to the lifespan of the factory instance.
/// Specifically, regression code needs to access the remapping tables that
/// tie HbrMesh vertices to their FarMesh counterparts for comparison.
///
template <class T, class U=T> class FarMeshFactory {

public:

    /// \brief Constructor for the factory.
    /// Analyzes the HbrMesh and stores transient data used to create the
    /// adaptive patch representation. Once the new rep has been instantiated
    /// with 'Create', this factory object can be deleted safely.
    ///
    /// @param mesh        The HbrMesh describing the topology (this mesh *WILL* be
    ///                    modified by this factory).
    ///
    /// @param maxlevel    In uniform subdivision mode : number of levels of
    ///                    subdivision. In feature adaptive mode : maximum
    ///                    level of isolation around extraordinary topological
    ///                    features.
    ///
    /// @param adaptive    Switch between uniform and feature adaptive mode
    ///
    /// @param firstLevel  First level of subdivision to use when building the
    ///                    FarMesh. The default -1 only generates a single patch
    ///                    array for the highest level of subdivision)
    ///                    Note : firstLevel is only applicable if adaptive is false
    ///
    /// @param patchType   The type of patch to create: QUADS or TRIANGLES
    ///                    Note : patchType is only applicable if adaptive is false
    ///
    /// @param kernelTypes A zero-terminated list of kernel types supported by the
    ///                    controller.
    ///                    Note : NULL indicates that all kernel types are supported
    ///
    FarMeshFactory(HbrMesh<T> * mesh, int maxlevel, bool adaptive=false, int firstLevel=-1,
                   FarPatchTables::Type patchType=FarPatchTables::QUADS,
                   const int * kernelTypes = NULL);

    /// \brief Create a table-based mesh representation
    ///
    /// @param requireFVarData create a face-varying table
    ///
    /// @return a pointer to the FarMesh created
    ///
    FarMesh<U> * Create( bool requireFVarData=false );

    /// \brief Computes the minimum number of adaptive feature isolation levels required
    /// in order for the limit surface to be an accurate representation of the
    /// shape given all the tags and edits.
    ///
    /// @param mesh           The HbrMesh describing the topology
    ///
    /// @param nfaces         The number of faces in the HbrMesh
    ///
    /// @param cornerIsolate  The level of isolation desired for patch corners
    ///
    /// @return               The minimum level of isolation of extraordinary
    ///                       topological features.
    ///
    static int ComputeMinIsolation( HbrMesh<T> const * mesh, int nfaces, int cornerIsolate=5 );

    /// \brief The Hbr mesh that this factory is converting
    HbrMesh<T> const * GetHbrMesh() const { return _hbrMesh; }

    /// \brief Maximum level of subidivision supported by this factory
    int GetMaxLevel() const { return _maxlevel; }

    /// \brief The number of coarse vertices found in the HbrMesh before refinement
    ///
    /// @return The number of coarse vertices
    ///
    int GetNumCoarseVertices() const { return _numCoarseVertices; }

    /// \brief Total number of faces up to a given level of subdivision
    ///
    /// @param level  The number of faces up to 'level' of subdivision
    ///
    /// @return       The summation of the number of faces
    ///
    int GetNumFacesTotal(int level) const {
        return sumList<HbrFace<T> *>(_facesList, level);
    }

    /// \brief Returns the corresponding index of the HbrVertex<T> in the new
    /// FarMesh
    ///
    /// @param v  the vertex
    ///
    /// @return   the remapped index of the vertex in the FarMesh
    ///
    int GetVertexID( HbrVertex<T> * v );

    /// \brief Returns the mapping between HbrVertex<T>->GetID() and Far
    /// vertices indices
    ///
    /// @return the table that maps HbrMesh to FarMesh vertex indices
    ///
    std::vector<int> const & GetRemappingTable( ) const { return _remapTable; }

    /// \brief Returns true if the specified kernel type is supported by the
    /// controller
    ///
    /// @return true if the kernel type is supported
    ///
    bool IsKernelTypeSupported( int kernelType ) const {
        assert(kernelType >= FarKernelBatch::FIRST_KERNEL_TYPE and
               kernelType < FarKernelBatch::NUM_KERNEL_TYPES);
        return _supportedKernelTypes[kernelType];
    }

    typedef std::vector<unsigned int> VertexList;
    typedef std::map<unsigned int, unsigned int> VertexPermutation;
    typedef std::vector<int> SplitTable;

    /// \brief Duplicates vertices at the finest subdivision level
    ///
    /// @param mesh  the mesh to modify
    ///
    /// @param vertexList  the list of vertices to duplicate
    ///
    static void DuplicateVertices( FarMesh<U> * mesh, VertexList const &vertexList);

    /// \brief Rearranges vertices to process them in a specific order
    ///
    /// @param mesh  the mesh to modify
    ///
    /// @param vertexPermutation  permutation of the vertices in a kernel batch
    ///
    static void PermuteVertices( FarMesh<U> * mesh, VertexPermutation const &vertexPermutation);

    /// \brief Splits patch control vertices that have been duplicated
    ///
    /// @param mesh  the mesh to modify
    ///
    /// @param splitTable  a table of offsets for each patch control vertex
    ///
    static void SplitVertices( FarMesh<U> * mesh, SplitTable const &splitTable );

private:
    friend class FarBilinearSubdivisionTablesFactory<T,U>;
    friend class FarCatmarkSubdivisionTablesFactory<T,U>;
    friend class FarLoopSubdivisionTablesFactory<T,U>;
    friend class FarSubdivisionTablesFactory<T,U>;
    friend class FarVertexEditTablesFactory<T,U>;
    friend class FarPatchTablesFactory<T>;

    template <class X> struct VertCompare {
        bool operator()(HbrVertex<X> const * v1, HbrVertex<X> const * v2 ) const {
            //return v1->GetID() < v2->GetID();
            return (void*)(v1) < (void*)(v2);
        }
    };

    // Non-copyable, so these are not implemented:
    FarMeshFactory( FarMeshFactory const & );
    FarMeshFactory<T,U> & operator=(FarMeshFactory<T,U> const &);

    // True if t1 and t2 are the same, even accounting for plugins
    static bool compareType(std::type_info const & t1, std::type_info const & t2);

    // True if the HbrMesh applies the bilinear subdivision scheme
    static bool isBilinear(HbrMesh<T> const * mesh);

    // True if the HbrMesh applies the Catmull-Clark subdivision scheme
    static bool isCatmark(HbrMesh<T> const * mesh);

    // True if the HbrMesh applies the Loop subdivision scheme
    static bool isLoop(HbrMesh<T> const * mesh);

    // True if the factory is refining adaptively
    bool isAdaptive() { return _adaptive; }

    // False if v prevents a face from being represented with a BSpline
    static bool vertexIsBSpline( HbrVertex<T> * v, bool next );

    // True if a vertex is a regular boundary
    static bool vertexIsRegularBoundary( HbrVertex<T> * v );

    // Non-const accessor to the remapping table
    std::vector<int> & getRemappingTable( ) { return _remapTable; }

    template <class Type> static int sumList( std::vector<std::vector<Type> > const & list, int level );

    // Calls Hbr to refines the neighbors of v
    static void refineVertexNeighbors(HbrVertex<T> * v);

    // Uniformly refine the Hbr mesh
    static void refine( HbrMesh<T> * mesh, int maxlevel );

    // Adaptively refine the Hbr mesh
    int refineAdaptive( HbrMesh<T> * mesh, int maxIsolate );

    typedef std::vector<std::vector< HbrFace<T> *> > FacesList;

    // Returns sorted vectors of HbrFace<T> pointers sorted by level
    FacesList const & GetFaceList() const { return _facesList; }

private:
    HbrMesh<T> * _hbrMesh;

    bool _adaptive;

    int _maxlevel,
        _firstlevel,
        _numVertices,
        _numCoarseVertices,
        _numFaces,
        _maxValence,
        _numPtexFaces;

    FarPatchTables::Type _patchType;

    bool _supportedKernelTypes[FarKernelBatch::NUM_KERNEL_TYPES];

    // remapping table to translate vertex ID's between Hbr indices and the
    // order of the same vertices in the tables
    std::vector<int> _remapTable;

    FacesList _facesList;
};

template <class T, class U>
    template <class Type> int
FarMeshFactory<T,U>::sumList( std::vector<std::vector<Type> > const & list, int level) {

    level = std::min(level, (int)list.size()-1);
    int total = 0;
    for (int i=0; i<=level; ++i)
        total += (int)list[i].size();
    return total;
}

// Refines non-adaptively an Hbr mesh
template <class T, class U> void
FarMeshFactory<T,U>::refine( HbrMesh<T> * mesh, int maxlevel ) {

    for (int level=0, firstface=0; level<maxlevel; ++level ) {

        int nfaces = mesh->GetNumFaces();

        for (int i=firstface; i<nfaces; ++i) {

            HbrFace<T> * f = mesh->GetFace(i);

            if (f->GetDepth()==level) {

                if (not f->IsHole()) {
                    f->Refine();
                }
            }
        }

        // Hbr allocates faces sequentially, so there is no need to iterate over
        // faces that have already been refined.
        firstface = nfaces;
    }
}

// Scan the faces of a mesh and compute the max level of subdivision required
template <class T, class U> int
FarMeshFactory<T,U>::ComputeMinIsolation( HbrMesh<T> const * mesh, int nfaces, int cornerIsolate ) {

    assert(mesh);


    int editmax=0;
    float sharpmax=0.0f;


    float cornerSharp=0.0;
    if (mesh->GetInterpolateBoundaryMethod()<HbrMesh<T>::k_InterpolateBoundaryEdgeAndCorner)
        cornerSharp = (float) cornerIsolate;

    // Check vertex sharpness
    int nverts = mesh->GetNumVertices();
    for (int i=0; i<nverts; ++i) {
        HbrVertex<T> * v = mesh->GetVertex(i);
        if (not v->OnBoundary())
            sharpmax = std::max( sharpmax, v->GetSharpness() );
        else {
            sharpmax = std::max( sharpmax, cornerSharp );
        }
    }

    // Check edge sharpness and hierarchical edits
    for (int i=0 ; i<nfaces ; ++i) {

        HbrFace<T> * f = mesh->GetFace(i);

        // We don't need to check non-coarse faces
        if (not f->IsCoarse())
            continue;

        // Check for edits
        if (f->HasVertexEdits()) {

            HbrVertexEdit<T> ** edits = (HbrVertexEdit<T>**)f->GetHierarchicalEdits();

            while (HbrVertexEdit<T> * edit = *edits++) {
                editmax = std::max( editmax , edit->GetNSubfaces() );
            }
        }

        // Check for sharpness
        int nv = f->GetNumVertices();
        for (int j=0; j<nv; ++j) {

            HbrHalfedge<T> * e = f->GetEdge(j);
            if (not e->IsBoundary())
                sharpmax = std::max( sharpmax, f->GetEdge(j)->GetSharpness() );
        }
    }

    int result = std::max( (int)ceil(sharpmax)+1, editmax+1 );

    // Cap the result to "infinitely sharp" (10)
    return std::min( result, (int)HbrHalfedge<T>::k_InfinitelySharp );
}

// True if a vertex is a regular boundary
template <class T, class U> bool
FarMeshFactory<T,U>::vertexIsRegularBoundary( HbrVertex<T> * v ) {
    int valence = v->GetValence();
    return (v->OnBoundary() and (valence==2 or valence==3));
}

// True if the vertex can be incorporated into a B-spline patch
template <class T, class U> bool
FarMeshFactory<T,U>::vertexIsBSpline( HbrVertex<T> * v, bool next ) {

    int valence = v->GetValence();

    // Boundary & corner vertices
    if (v->OnBoundary()) {
        if (valence==2) {
            // corner vertex

            HbrFace<T> * f = v->GetFace();
            // the vertex may not need isolation depending on boundary
            // interpolation rule (sharp vs. rounded corner)
            typename HbrMesh<T>::InterpolateBoundaryMethod method =
                f->GetMesh()->GetInterpolateBoundaryMethod();
            if (method==HbrMesh<T>::k_InterpolateBoundaryEdgeAndCorner) {
                if (not next) {
                    // if we are checking coarse vertices (next==false),
                    // count the number of corners in the face, because we
                    // can only have 1 corner vertex in a corner patch.
                    int nsharpboundaries=0;
                    for (int i=0; i<f->GetNumVertices(); ++i) {
                        HbrHalfedge<T> * e = f->GetEdge(i);
                        if (e->IsBoundary() and
                            e->GetSharpness()==HbrHalfedge<T>::k_InfinitelySharp) {
                            ++nsharpboundaries;
                        }
                    }
                    return nsharpboundaries < 3 ? true: false;
                } else
                    return true;
            } else
                return false;
        } else if (valence>3) {
            // extraordinary boundar vertex (high valence)
            return false;
        }
        // regular boundary vertices have valence 3
        return true;
    }

    // Extraordinary or creased vertices that aren't corner / boundaries
    if (v->IsExtraordinary() or v->IsSharp(next))
        return false;

    return true;
}

// Calls Hbr to refines the neighbors of v
template <class T, class U> void
FarMeshFactory<T,U>::refineVertexNeighbors(HbrVertex<T> * v) {

    assert(v);

    HbrHalfedge<T> * start = v->GetIncidentEdge(),
                   * next=start;
    do {

        HbrFace<T> * lft = next->GetLeftFace(),
                   * rgt = next->GetRightFace();

        if (not ((lft and lft->IsHole()) and
                 (rgt and rgt->IsHole()) ) ) {

            if (rgt)
                rgt->_adaptiveFlags.isTagged=true;

            if (lft)
                lft->_adaptiveFlags.isTagged=true;

            HbrHalfedge<T> * istart = next,
                           * inext = istart;
            do {
                if (not inext->IsInsideHole()  )
                    inext->GetOrgVertex()->Refine();
                inext = inext->GetNext();
            } while (istart != inext);
        }
        next = v->GetNextEdge( next );
    } while (next and next!=start);
}


// Refines an Hbr Catmark mesh adaptively around extraordinary features
template <class T, class U> int
FarMeshFactory<T,U>::refineAdaptive( HbrMesh<T> * mesh, int maxIsolate ) {

    int ncoarsefaces = mesh->GetNumCoarseFaces(),
        ncoarseverts = mesh->GetNumVertices();

    // First pass : tag coarse vertices & faces that need refinement

    typedef std::set<HbrVertex<T> *,VertCompare<T> > VertSet;
    VertSet verts, nextverts;

    for (int i=0; i<ncoarseverts; ++i) {
        HbrVertex<T> * v = mesh->GetVertex(i);

        // Non manifold topology may leave un-connected vertices that need to be skipped
        if (not v->IsConnected()) {
            continue;
        }

        // Tag non-BSpline vertices for refinement
        if (not vertexIsBSpline(v, false)) {
            v->_adaptiveFlags.isTagged=true;
            nextverts.insert(v);
        }
    }

    for (int i=0; i<ncoarsefaces; ++i) {
        HbrFace<T> * f = mesh->GetFace(i);

        if (f->IsHole())
            continue;

        bool extraordinary = mesh->GetSubdivision()->FaceIsExtraordinary(mesh,f);

        int nv = f->GetNumVertices();
        for (int j=0; j<nv; ++j) {

            HbrHalfedge<T> * e = f->GetEdge(j);
            assert(e);

            // Tag sharp edges for refinement
            if (e->IsSharp(true) and (not e->IsBoundary())) {
                nextverts.insert(e->GetOrgVertex());
                nextverts.insert(e->GetDestVertex());

                e->GetOrgVertex()->_adaptiveFlags.isTagged=true;
                e->GetDestVertex()->_adaptiveFlags.isTagged=true;
            }

            // Tag extraordinary (non-quad) faces for refinement
            if (extraordinary or f->HasVertexEdits()) {
                HbrVertex<T> * v = f->GetVertex(j);
                v->_adaptiveFlags.isTagged=true;
                nextverts.insert(v);
            }

            // Quad-faces with 2 non-consecutive boundaries need to be flagged
            // for refinement as boundary patches.
            //
            //  o ........ o ........ o ........ o
            //  .          |          |          .     ... boundary edge
            //  .          |   needs  |          .
            //  .          |   flag   |          .     --- regular edge
            //  .          |          |          .
            //  o ........ o ........ o ........ o
            //
            if ( e->IsBoundary() and (not f->_adaptiveFlags.isTagged) and nv==4 ) {

                if (e->GetPrev() and (not e->GetPrev()->IsBoundary()) and
                    e->GetNext() and (not e->GetNext()->IsBoundary()) and
                    e->GetNext() and e->GetNext()->GetNext() and e->GetNext()->GetNext()->IsBoundary()) {

                    // Tag the face so that we don't check for this again
                    f->_adaptiveFlags.isTagged=true;

                    // Tag all 4 vertices of the face to make sure 4 boundary
                    // sub-patches are generated
                    for (int k=0; k<4; ++k) {
                        HbrVertex<T> * v = f->GetVertex(k);
                        v->_adaptiveFlags.isTagged=true;
                        nextverts.insert(v);
                    }
                }
            }
        }
        _maxValence = std::max(_maxValence, nv);
    }


    // Second pass : refine adaptively around singularities

    for (int level=0; level<maxIsolate; ++level) {

        verts = nextverts;
        nextverts.clear();

        // Refine vertices
        for (typename VertSet::iterator i=verts.begin(); i!=verts.end(); ++i) {

            HbrVertex<T> * v = *i;
            assert(v);

            if (level>0)
                v->_adaptiveFlags.isTagged=true;
            else
                v->_adaptiveFlags.wasTagged=true;

            refineVertexNeighbors(v);

            // Tag non-BSpline vertices for refinement
            if (not vertexIsBSpline(v, true))
                nextverts.insert(v->Subdivide());

            // Refine edges with creases or edits
            int valence = v->GetValence();
            _maxValence = std::max(_maxValence, valence);

            HbrHalfedge<T> * e = v->GetIncidentEdge();
            for (int j=0; j<valence; ++j) {

                // Skip edges that have already been processed (HasChild())
                if ((not e->HasChild()) and e->IsSharp(false) and (not e->IsBoundary())) {

                    if (not e->IsInsideHole()) {
                        nextverts.insert( e->Subdivide() );
                        nextverts.insert( e->GetOrgVertex()->Subdivide() );
                        nextverts.insert( e->GetDestVertex()->Subdivide() );
                    }
                }
                HbrHalfedge<T> * next = v->GetNextEdge(e);
                e = next ? next : e->GetPrev();
            }

            // Flag verts with hierarchical edits for neighbor refinement at the next level
            HbrVertex<T> * childvert = v->Subdivide();
            HbrHalfedge<T> * childedge = childvert->GetIncidentEdge();
            assert( childvert->GetValence()==valence);
            for (int j=0; j<valence; ++j) {
                HbrFace<T> * f = childedge->GetFace();
                if (f->HasVertexEdits()) {
                    int nv = f->GetNumVertices();
                    for (int k=0; k<nv; ++k)
                        nextverts.insert( f->GetVertex(k) );
                }
                if ((childedge = childvert->GetNextEdge(childedge)) == NULL)
                    break;
            }
        }

        // Add coarse verts from extraordinary faces
        if (level==0) {
            for (int i=0; i<ncoarsefaces; ++i) {
                HbrFace<T> * f = mesh->GetFace(i);
                assert (f->IsCoarse());

                if (mesh->GetSubdivision()->FaceIsExtraordinary(mesh,f))
                    nextverts.insert( f->Subdivide() );
            }
        }
    }
    return maxIsolate;
}

// Assumption : the order of the vertices in the HbrMesh could be set in any
// random order, so the builder runs 2 passes over the entire vertex list to
// gather the counters needed to generate the indexing tables.
template <class T, class U>
FarMeshFactory<T,U>::FarMeshFactory( HbrMesh<T> * mesh, int maxlevel, bool adaptive,
    int firstlevel, FarPatchTables::Type patchType, const int * kernelTypes ) :
    _hbrMesh(mesh),
    _adaptive(adaptive),
    _maxlevel(maxlevel),
    _firstlevel(firstlevel),
    _numVertices(-1),
    _numCoarseVertices(-1),
    _numFaces(-1),
    _maxValence(4),
    _numPtexFaces(-1),
    _patchType(patchType),
    _facesList(maxlevel+1)
{
    _numCoarseVertices = mesh->GetNumVertices();
    _numPtexFaces = getNumPtexFaces(mesh);

    // Select the kernel types that are supported by the controller.
    for (int i = FarKernelBatch::FIRST_KERNEL_TYPE; i < FarKernelBatch::NUM_KERNEL_TYPES; ++i) {
        _supportedKernelTypes[i] = kernelTypes ? false : true;
    }

    for (int i = kernelTypes ? *kernelTypes++ : 0; i; i = *kernelTypes++) {
        assert(i >= FarKernelBatch::FIRST_KERNEL_TYPE and
               i < FarKernelBatch::NUM_KERNEL_TYPES);
        _supportedKernelTypes[i] = true;
    }

    // Subdivide the Hbr mesh up to maxlevel.
    //
    // Note : using a placeholder vertex class 'T' can greatly speed up the
    // topological analysis if the interpolation results are not used.
    if (adaptive)
        _maxlevel=refineAdaptive( mesh, maxlevel );
    else
        refine( mesh, maxlevel);

    _numFaces = mesh->GetNumFaces();

    _numVertices = mesh->GetNumVertices();

    if (not adaptive) {

        // Populate the face lists
        int fsize=0;
        for (int i=0; i<_numFaces; ++i) {
            HbrFace<T> * f = mesh->GetFace(i);
            assert(f);
            if (f->GetDepth()==0 and (not f->IsHole()))
                fsize += mesh->GetSubdivision()->GetFaceChildrenCount( f->GetNumVertices() );
        }

        _facesList[0].reserve(mesh->GetNumCoarseFaces());
        _facesList[1].reserve(fsize);
        for (int l=2; l<=maxlevel; ++l)
            _facesList[l].reserve( _facesList[l-1].capacity()*4 );

        for (int i=0; i<_numFaces; ++i) {
            HbrFace<T> * f = mesh->GetFace(i);
            if (f->GetDepth()<=maxlevel and (not f->IsHole()))
                _facesList[ f->GetDepth() ].push_back(f);
        }
    }
}

template <class T, class U> bool
FarMeshFactory<T,U>::compareType(std::type_info const & t1, std::type_info const & t2) {

    if (t1==t2) {
        return true;
    }

    // On some systems, distinct instances of \c type_info objects compare equal if
    // their name() functions return equivalent strings.  On other systems, distinct
    // type_info objects never compare equal.  The latter can cause problems in the
    // presence of plugins loaded without RTLD_GLOBAL, because typeid(T) returns
    // different \c type_info objects for the same T in the two plugins.
    for (char const * p1 = t1.name(), *p2 = t2.name(); *p1 == *p2; ++p1, ++p2)
        if (*p1 == '\0')
            return true;
    return false;
}

template <class T, class U> bool
FarMeshFactory<T,U>::isBilinear(HbrMesh<T> const * mesh) {
    return compareType(typeid(*(mesh->GetSubdivision())), typeid(HbrBilinearSubdivision<T>));
}

template <class T, class U> bool
FarMeshFactory<T,U>::isCatmark(HbrMesh<T> const * mesh) {
    return compareType(typeid(*(mesh->GetSubdivision())), typeid(HbrCatmarkSubdivision<T>));
}

template <class T, class U> bool
FarMeshFactory<T,U>::isLoop(HbrMesh<T> const * mesh) {
    return compareType(typeid(*(mesh->GetSubdivision())), typeid(HbrLoopSubdivision<T>));
}

template <class T, class U> void
copyVertex( T & /* dest */, U const & /* src */ ) {
}

template <class T> void
copyVertex( T & dest, T const & src ) {
    dest = src;
}

template <class T> int
getNumPtexFaces(HbrMesh<T> const * hmesh) {

    HbrFace<T> * lastface = hmesh->GetFace(hmesh->GetNumFaces()-1);
    assert(lastface);

    int result = lastface->GetPtexIndex();

    result += (hmesh->GetSubdivision()->FaceIsExtraordinary(hmesh, lastface) ?
                  lastface->GetNumVertices() : 1);

    return result;
}

template <class T, class U> FarMesh<U> *
FarMeshFactory<T,U>::Create( bool requireFVarData ) {

    assert( GetHbrMesh() );

    // Note : we cannot create a Far rep of level 0 (coarse mesh)
    if (GetMaxLevel()<1)
        return 0;

    FarMesh<U> * result = new FarMesh<U>();

    if ( isBilinear( GetHbrMesh() ) ) {
        result->_subdivisionTables = FarBilinearSubdivisionTablesFactory<T,U>::Create(this, &result->_batches);
    } else if ( isCatmark( GetHbrMesh() ) ) {
        result->_subdivisionTables = FarCatmarkSubdivisionTablesFactory<T,U>::Create(this, &result->_batches);
    } else if ( isLoop(GetHbrMesh()) ) {
        result->_subdivisionTables = FarLoopSubdivisionTablesFactory<T,U>::Create(this, &result->_batches);
    } else
        assert(0);
    assert(result->_subdivisionTables);

    // If the vertex classes aren't place-holders, copy the data of the coarse
    // vertices into the vertex buffer.
    if (sizeof(U)>1) {
        result->_vertices.resize( _numVertices );
        for (int i=0; i<GetNumCoarseVertices(); ++i)
            copyVertex(result->_vertices[i], GetHbrMesh()->GetVertex(i)->GetData());
    }

    int fvarwidth = requireFVarData ? _hbrMesh->GetTotalFVarWidth() : 0;

    // Create the element indices tables (patches for adaptive, quads for non-adaptive)
    if (isAdaptive()) {

        FarPatchTablesFactory<T> factory(GetHbrMesh(), _numFaces, _remapTable);

        // XXXX: currently PatchGregory shader supports up to 29 valence
        result->_patchTables = factory.Create(_maxValence, _numPtexFaces, fvarwidth);

    } else {
        result->_patchTables = FarPatchTablesFactory<T>::Create(GetHbrMesh(), _facesList, _remapTable, _firstlevel, _patchType, _numPtexFaces, fvarwidth );
    }
    assert( result->_patchTables );

    // Create VertexEditTables if necessary
    if (GetHbrMesh()->HasVertexEdits()) {
        result->_vertexEditTables = FarVertexEditTablesFactory<T,U>::Create( this, result, &result->_batches, GetMaxLevel() );
        assert(result->_vertexEditTables);
    }

    return result;
}

template <class T, class U> int
FarMeshFactory<T,U>::GetVertexID( HbrVertex<T> * v ) {
    assert( v  and (v->GetID() < _remapTable.size()) );
    return _remapTable[ v->GetID() ];
}

template <class T, class U> void
FarMeshFactory<T, U>::DuplicateVertices( FarMesh<U> * mesh,
    VertexList const &vertexList )
{
    FarKernelBatchVector& kernelBatchVector = mesh->_batches;
    FarPatchTables* patchTables = mesh->_patchTables;
    FarSubdivisionTables* subdivisionTables = mesh->_subdivisionTables;
    assert(subdivisionTables->GetScheme() == FarSubdivisionTables::CATMARK);

    VertexList sortedVertexList(vertexList);
    std::sort(sortedVertexList.begin(), sortedVertexList.end());

    for (FarKernelBatchVector::iterator i = kernelBatchVector.begin();
        i != kernelBatchVector.end(); ++i)
    {
        FarKernelBatch& kernelBatch = *i;

        VertexList::iterator begin =
            std::lower_bound(sortedVertexList.begin(),
            sortedVertexList.end(),
            kernelBatch.GetVertexOffset() + kernelBatch.GetStart());
        VertexList::iterator end =
            std::upper_bound(sortedVertexList.begin(),
            sortedVertexList.end(),
            kernelBatch.GetVertexOffset() + kernelBatch.GetEnd() - 1);
        if (begin == sortedVertexList.end() ||
            (int)*begin >= kernelBatch.GetVertexOffset() + kernelBatch.GetEnd())
        {
            continue; // the vertices of the kernel batch are not duplicated
        }

        // Guarantee that the kernel batch is at the finest subdivision level.
        assert(kernelBatch.GetLevel() == subdivisionTables->GetMaxLevel() - 1);

        // Duplicate the vertices in this kernel batch.
        FarCatmarkSubdivisionTablesFactory<T, U>::DuplicateVertices(
            subdivisionTables, kernelBatch, VertexList(begin, end));

        // Shift the affected kernel batches.
        FarKernelBatchVector::iterator first = i;
        FarKernelBatchVector::iterator last = kernelBatchVector.end();
        for (++first; first != last; ++first) {
            FarCatmarkSubdivisionTablesFactory<T, U>::ShiftVertices(
                subdivisionTables, *first, kernelBatch,
                std::distance(begin, end));
        }

        // Shift the control vertices in the patch tables.
        FarPatchTablesFactory<T>::ShiftVertices(patchTables, kernelBatch,
            std::distance(begin, end));
    }
}

template <class T, class U> void
FarMeshFactory<T, U>::PermuteVertices( FarMesh<U> * mesh,
    VertexPermutation const &vertexPermutation )
{
    FarKernelBatchVector& kernelBatchVector = mesh->_batches;
    FarPatchTables* patchTables = mesh->_patchTables;
    FarSubdivisionTables* subdivisionTables = mesh->_subdivisionTables;
    assert(subdivisionTables->GetScheme() == FarSubdivisionTables::CATMARK);

    for (FarKernelBatchVector::const_iterator i = kernelBatchVector.begin();
        i != kernelBatchVector.end(); ++i)
    {
        const FarKernelBatch& kernelBatch = *i;

        // Permute the vertices in this kernel batch.
        if (not FarCatmarkSubdivisionTablesFactory<T, U>::PermuteVertices(
            subdivisionTables, kernelBatch, vertexPermutation))
        {
            continue;
        }

        // Find the range of kernel batches affected by the vertex permutation.
        FarKernelBatchVector::const_iterator first = i;
        FarKernelBatchVector::const_iterator last = kernelBatchVector.end();
        for (FarKernelBatchVector::const_iterator j = first; j != last; ++j) {
            if (j->GetLevel() > kernelBatch.GetLevel() + 1) {
                // The vertex permutation does not affect this level.
                last = j;
                break;
            }
        }

        // Remap the vertices in the affected kernel batches.
        for (++first; first != last; ++first) {
            FarCatmarkSubdivisionTablesFactory<T, U>::RemapVertices(
                subdivisionTables, *first, vertexPermutation);
        }

        // Remap the patch tables.
        FarPatchTablesFactory<T>::RemapVertices(patchTables, vertexPermutation);
    }
}

template <class T, class U> void
FarMeshFactory<T, U>::SplitVertices( FarMesh<U> * mesh,
    SplitTable const &splitTable )
{
    FarPatchTables* patchTables = mesh->_patchTables;
    FarPatchTablesFactory<T>::SplitVertices(patchTables, splitTable);
}

} // end namespace OPENSUBDIV_VERSION
using namespace OPENSUBDIV_VERSION;

} // end namespace OpenSubdiv

#endif /* FAR_MESH_FACTORY_H */
