#include "AlembicPolyMsh.h"
#include "AlembicXform.h"

#include <xsi_application.h>
#include <xsi_x3dobject.h>
#include <xsi_primitive.h>
#include <xsi_geometry.h>
#include <xsi_polygonmesh.h>
#include <xsi_vertex.h>
#include <xsi_polygonface.h>
#include <xsi_sample.h>
#include <xsi_math.h>
#include <xsi_context.h>
#include <xsi_operatorcontext.h>
#include <xsi_customoperator.h>
#include <xsi_factory.h>
#include <xsi_parameter.h>
#include <xsi_ppglayout.h>
#include <xsi_ppgitem.h>
#include <xsi_kinematics.h>
#include <xsi_kinematicstate.h>
#include <xsi_clusterproperty.h>
#include <xsi_cluster.h>
#include <xsi_geometryaccessor.h>
#include <xsi_material.h>
#include <xsi_materiallibrary.h>
#include <xsi_iceattribute.h>
#include <xsi_iceattributedataarray.h>
#include "CommonProfiler.h"
#include "CommonMeshUtilities.h"

using namespace XSI;
using namespace MATH;

namespace AbcA = ::Alembic::AbcCoreAbstract::ALEMBIC_VERSION_NS;
namespace AbcB = ::Alembic::Abc::ALEMBIC_VERSION_NS;
using namespace AbcA;
using namespace AbcB;

AlembicPolyMesh::AlembicPolyMesh(const XSI::CRef & in_Ref, AlembicWriteJob * in_Job)
: AlembicObject(in_Ref, in_Job)
{
   Primitive prim(GetRef());
   CString meshName(prim.GetParent3DObject().GetName());
   CString xformName(meshName+L"Xfo");
   Alembic::AbcGeom::OXform xform(GetOParent(),xformName.GetAsciiString(),GetJob()->GetAnimatedTs());
   Alembic::AbcGeom::OPolyMesh mesh(xform,meshName.GetAsciiString(),GetJob()->GetAnimatedTs());
   AddRef(prim.GetParent3DObject().GetKinematics().GetGlobal().GetRef());

   mXformSchema = xform.getSchema();
   mMeshSchema = mesh.getSchema();

   // create the generic properties
   mOVisibility = CreateVisibilityProperty(mesh,GetJob()->GetAnimatedTs());
}

AlembicPolyMesh::~AlembicPolyMesh()
{
   // we have to clear this prior to destruction
   // this is a workaround for issue-171
   mOVisibility.reset();
}

Alembic::Abc::OCompoundProperty AlembicPolyMesh::GetCompound()
{
   return mMeshSchema;
}

XSI::CStatus AlembicPolyMesh::Save(double time)
{
   // store the transform
   Primitive prim(GetRef());
   bool globalSpace = GetJob()->GetOption(L"globalSpace");
   SaveXformSample(GetRef(1),mXformSchema,mXformSample,time,false,globalSpace);

   // query the global space
   CTransformation globalXfo;
   if(globalSpace)
      globalXfo = Kinematics(KinematicState(GetRef(1)).GetParent()).GetGlobal().GetTransform(time);
   CTransformation globalRotation;
   globalRotation.SetRotation(globalXfo.GetRotation());

   // store the metadata
   SaveMetaData(prim.GetParent3DObject().GetRef(),this);

   // set the visibility
   Property visProp;
   prim.GetParent3DObject().GetPropertyFromName(L"Visibility",visProp);
   if(isRefAnimated(visProp.GetRef()) || mNumSamples == 0)
   {
      bool visibility = visProp.GetParameterValue(L"rendvis",time);
      mOVisibility.set(visibility ? Alembic::AbcGeom::kVisibilityVisible : Alembic::AbcGeom::kVisibilityHidden);
   }

   // check if the mesh is animated
   if(mNumSamples > 0) {
      if(!isRefAnimated(GetRef(),false,globalSpace))
         return CStatus::OK;
   }

   // check if we just have a pure pointcache (no surface)
   bool purePointCache = (bool)GetJob()->GetOption(L"exportPurePointCache");

   // define additional vectors, necessary for this task
   std::vector<Alembic::Abc::V3f> posVec;
   std::vector<Alembic::Abc::N3f> normalVec;
   std::vector<uint32_t> normalIndexVec;

   // access the mesh
   PolygonMesh mesh = prim.GetGeometry(time);
   CVector3Array pos = mesh.GetVertices().GetPositionArray();
   LONG vertCount = pos.GetCount();

   // prepare the bounding box
   Alembic::Abc::Box3d bbox;

   // allocate the points and normals
   posVec.resize(vertCount);
   for(LONG i=0;i<vertCount;i++)
   {
      if(globalSpace)
         pos[i] = MapObjectPositionToWorldSpace(globalXfo,pos[i]);
      posVec[i].x = (float)pos[i].GetX();
      posVec[i].y = (float)pos[i].GetY();
      posVec[i].z = (float)pos[i].GetZ();
      bbox.extendBy(posVec[i]);
   }

   // allocate the sample for the points
   if(posVec.size() == 0)
   {
      bbox.extendBy(Alembic::Abc::V3f(0,0,0));
      posVec.push_back(Alembic::Abc::V3f(FLT_MAX,FLT_MAX,FLT_MAX));
   }
   Alembic::Abc::P3fArraySample posSample(&posVec.front(),posVec.size());

   // store the positions && bbox
   mMeshSample.setPositions(posSample);
   mMeshSample.setSelfBounds(bbox);

   // abort here if we are just storing points
   if(purePointCache)
   {
      if(mNumSamples == 0)
      {
         // store a dummy empty topology
         mMeshSample.setFaceCounts(Alembic::Abc::Int32ArraySample(NULL, 0));
         mMeshSample.setFaceIndices(Alembic::Abc::Int32ArraySample(NULL, 0));
      }

      mMeshSchema.set(mMeshSample);
      mNumSamples++;
      return CStatus::OK;
   }

   // check if we support changing topology
   bool dynamicTopology = (bool)GetJob()->GetOption(L"exportDynamicTopology");

   // get the faces
   CPolygonFaceRefArray faces = mesh.GetPolygons();
   LONG faceCount = faces.GetCount();
   LONG sampleCount = mesh.GetSamples().GetCount();

   // create a sample look table
   LONG offset = 0;
   CLongArray sampleLookup(sampleCount);
   for(LONG i=0;i<faces.GetCount();i++)
   {
      PolygonFace face(faces[i]);
      CLongArray samples = face.GetSamples().GetIndexArray();
      for(LONG j=samples.GetCount()-1;j>=0;j--)
         sampleLookup[offset++] = samples[j];
   }

   // let's check if we have user normals
   size_t normalCount = 0;
   size_t normalIndexCount = 0;
   bool exportNormals = GetJob()->GetOption(L"exportNormals");
   if(exportNormals)
   {
      CVector3Array normals = mesh.GetVertices().GetNormalArray();

      CGeometryAccessor accessor = mesh.GetGeometryAccessor(siConstructionModeSecondaryShape);
      CRefArray userNormalProps = accessor.GetUserNormals();
      CFloatArray shadingNormals;
      accessor.GetNodeNormals(shadingNormals);
      if(userNormalProps.GetCount() > 0)
      {
         ClusterProperty userNormalProp(userNormalProps[0]);
         Cluster cluster(userNormalProp.GetParent());
         CLongArray elements = cluster.GetElements().GetArray();
         CDoubleArray userNormals = userNormalProp.GetElements().GetArray();
         for(LONG i=0;i<elements.GetCount();i++)
         {
            LONG sampleIndex = elements[i] * 3;
            if(sampleIndex >= shadingNormals.GetCount())
               continue;
            shadingNormals[sampleIndex++] = (float)userNormals[i*3+0];
            shadingNormals[sampleIndex++] = (float)userNormals[i*3+1];
            shadingNormals[sampleIndex++] = (float)userNormals[i*3+2];
         }
      }
      normalVec.resize(shadingNormals.GetCount() / 3);
      normalCount = normalVec.size();

      for(LONG i=0;i<sampleCount;i++)
      {
         LONG lookedup = sampleLookup[i];
         CVector3 normal;
         normal.PutX(shadingNormals[lookedup * 3 + 0]);
         normal.PutY(shadingNormals[lookedup * 3 + 1]);
         normal.PutZ(shadingNormals[lookedup * 3 + 2]);
         if(globalSpace)
         {
            normal = MapObjectPositionToWorldSpace(globalRotation,normal);
            normal.NormalizeInPlace();
         }
         normalVec[i].x = (float)normal.GetX();
         normalVec[i].y = (float)normal.GetY();
         normalVec[i].z = (float)normal.GetZ();
      }

      // now let's sort the normals 
      if((bool)GetJob()->GetOption(L"indexedNormals")) {
         std::map<SortableV3f,size_t> normalMap;
         std::map<SortableV3f,size_t>::const_iterator it;
         size_t sortedNormalCount = 0;
         std::vector<Alembic::Abc::V3f> sortedNormalVec;
         normalIndexVec.resize(normalVec.size());
         sortedNormalVec.resize(normalVec.size());

         // loop over all normals
         for(size_t i=0;i<normalVec.size();i++)
         {
            it = normalMap.find(normalVec[i]);
            if(it != normalMap.end())
               normalIndexVec[normalIndexCount++] = (uint32_t)it->second;
            else
            {
               normalIndexVec[normalIndexCount++] = (uint32_t)sortedNormalCount;
               normalMap.insert(std::pair<Alembic::Abc::V3f,size_t>(normalVec[i],(uint32_t)sortedNormalCount));
               sortedNormalVec[sortedNormalCount++] = normalVec[i];
            }
         }

         // use indexed normals if they use less space
         normalVec = sortedNormalVec;
         normalCount = sortedNormalCount;

         sortedNormalCount = 0;
         sortedNormalVec.clear();
      }
   }

   // check if we should export the velocities
   if(dynamicTopology)
   {
      ICEAttribute velocitiesAttr = mesh.GetICEAttributeFromName(L"PointVelocity");
      if(velocitiesAttr.IsDefined() && velocitiesAttr.IsValid())
      {
         CICEAttributeDataArrayVector3f velocitiesData;
         velocitiesAttr.GetDataArray(velocitiesData);

         mVelocitiesVec.resize(vertCount);
         for(LONG i=0;i<vertCount;i++)
         {
            CVector3 vel;
            vel.PutX(velocitiesData[i].GetX());
            vel.PutY(velocitiesData[i].GetY());
            vel.PutZ(velocitiesData[i].GetZ());
            if(globalSpace)
               vel = MapObjectPositionToWorldSpace(globalRotation,vel);
            mVelocitiesVec[i].x = (float)vel.GetX();
            mVelocitiesVec[i].y = (float)vel.GetY();
            mVelocitiesVec[i].z = (float)vel.GetZ();
         }

         if(mVelocitiesVec.size() == 0)
            mVelocitiesVec.push_back(Alembic::Abc::V3f(0,0,0));


         Alembic::Abc::V3fArraySample sample = Alembic::Abc::V3fArraySample(&mVelocitiesVec.front(),mVelocitiesVec.size());
         mMeshSample.setVelocities(sample);
      }
   }

   // if we are the first frame!
   if(mNumSamples == 0 || (dynamicTopology))
   {
      // we also need to store the face counts as well as face indices
      mFaceCountVec.resize(faceCount);
      mFaceIndicesVec.resize(sampleCount);

      offset = 0;
      for(LONG i=0;i<faceCount;i++)
      {
         PolygonFace face(faces[i]);
         CLongArray indices = face.GetVertices().GetIndexArray();
         mFaceCountVec[i] = indices.GetCount();
         for(LONG j=indices.GetCount()-1;j>=0;j--)
            mFaceIndicesVec[offset++] = indices[j];
      }

      if(mFaceIndicesVec.size() == 0)
      {
         mFaceCountVec.push_back(0);
         mFaceIndicesVec.push_back(0);
      }
      Alembic::Abc::Int32ArraySample faceCountSample(&mFaceCountVec.front(),mFaceCountVec.size());
      Alembic::Abc::Int32ArraySample faceIndicesSample(&mFaceIndicesVec.front(),mFaceIndicesVec.size());

      mMeshSample.setFaceCounts(faceCountSample);
      mMeshSample.setFaceIndices(faceIndicesSample);

      Alembic::AbcGeom::ON3fGeomParam::Sample normalSample;
      if((normalVec.size() == 0 || normalCount == 0) && exportNormals && dynamicTopology)
      {
         normalVec.push_back(Alembic::Abc::V3f(FLT_MAX,FLT_MAX,FLT_MAX));
         normalCount = 1;
         normalIndexVec.push_back(0);
         normalIndexCount = 1;
      }
      if(normalVec.size() > 0 && normalCount > 0)
      {
         normalSample.setScope(Alembic::AbcGeom::kFacevaryingScope);
         normalSample.setVals(Alembic::Abc::N3fArraySample(&normalVec.front(),normalCount));
         if(normalIndexCount > 0)
            normalSample.setIndices(Alembic::Abc::UInt32ArraySample(&normalIndexVec.front(),normalIndexCount));
         mMeshSample.setNormals(normalSample);
      }

      // also check if we need to store UV
      CRefArray clusters = mesh.GetClusters();
      if((bool)GetJob()->GetOption(L"exportUVs"))
      {
         CGeometryAccessor accessor = mesh.GetGeometryAccessor(siConstructionModeSecondaryShape);
         CRefArray uvPropRefs = accessor.GetUVs();

         // if we now finally found a valid uvprop
         if(uvPropRefs.GetCount() > 0)
         {
            // ok, great, we found UVs, let's set them up
            if(mNumSamples == 0)
            {
               mUvVec.resize(uvPropRefs.GetCount());
               if((bool)GetJob()->GetOption(L"indexedUVs"))
                  mUvIndexVec.resize(uvPropRefs.GetCount());

               // query the names of all uv properties
               std::vector<std::string> uvSetNames;
               for(LONG i=0;i< uvPropRefs.GetCount();i++)
                  uvSetNames.push_back(ClusterProperty(uvPropRefs[i]).GetName().GetAsciiString());

               Alembic::Abc::OStringArrayProperty uvSetNamesProperty = Alembic::Abc::OStringArrayProperty(
                  mMeshSchema, ".uvSetNames", mMeshSchema.getMetaData(), GetJob()->GetAnimatedTs() );
               Alembic::Abc::StringArraySample uvSetNamesSample(&uvSetNames.front(),uvSetNames.size());
               uvSetNamesProperty.set(uvSetNamesSample);
            }

            // loop over all uvsets
            for(LONG uvI=0;uvI<uvPropRefs.GetCount();uvI++)
            {
               mUvVec[uvI].resize(sampleCount);
               CDoubleArray uvValues = ClusterProperty(uvPropRefs[uvI]).GetElements().GetArray();

               for(LONG i=0;i<sampleCount;i++)
               {
                  mUvVec[uvI][i].x = (float)uvValues[sampleLookup[i] * 3 + 0];
                  mUvVec[uvI][i].y = (float)uvValues[sampleLookup[i] * 3 + 1];
               }

               // now let's sort the normals 
               size_t uvCount = mUvVec[uvI].size();
               size_t uvIndexCount = 0;
               if((bool)GetJob()->GetOption(L"indexedUVs")) {
                  std::map<SortableV2f,size_t> uvMap;
                  std::map<SortableV2f,size_t>::const_iterator it;
                  size_t sortedUVCount = 0;
                  std::vector<Alembic::Abc::V2f> sortedUVVec;
                  mUvIndexVec[uvI].resize(mUvVec[uvI].size());
                  sortedUVVec.resize(mUvVec[uvI].size());

                  // loop over all uvs
                  for(size_t i=0;i<mUvVec[uvI].size();i++)
                  {
                     it = uvMap.find(mUvVec[uvI][i]);
                     if(it != uvMap.end())
                        mUvIndexVec[uvI][uvIndexCount++] = (uint32_t)it->second;
                     else
                     {
                        mUvIndexVec[uvI][uvIndexCount++] = (uint32_t)sortedUVCount;
                        uvMap.insert(std::pair<Alembic::Abc::V2f,size_t>(mUvVec[uvI][i],(uint32_t)sortedUVCount));
                        sortedUVVec[sortedUVCount++] = mUvVec[uvI][i];
                     }
                  }

                  // use indexed uvs if they use less space
                  mUvVec[uvI] = sortedUVVec;
                  uvCount = sortedUVCount;

                  sortedUVCount = 0;
                  sortedUVVec.clear();
               }

               Alembic::AbcGeom::OV2fGeomParam::Sample uvSample(Alembic::Abc::V2fArraySample(&mUvVec[uvI].front(),uvCount),Alembic::AbcGeom::kFacevaryingScope);
               if(mUvIndexVec.size() > 0 && uvIndexCount > 0)
                  uvSample.setIndices(Alembic::Abc::UInt32ArraySample(&mUvIndexVec[uvI].front(),uvIndexCount));

               if(uvI == 0)
               {
                  mMeshSample.setUVs(uvSample);
               }
               else
               {
                  // create the uv param if required
                  if(mNumSamples == 0)
                  {
                     CString storedUvSetName = CString(L"uv") + CString(uvI);
                     mUvParams.push_back(Alembic::AbcGeom::OV2fGeomParam( mMeshSchema, storedUvSetName.GetAsciiString(), uvIndexCount > 0,
                                         Alembic::AbcGeom::kFacevaryingScope, 1, GetJob()->GetAnimatedTs()));
                  }
                  mUvParams[uvI-1].set(uvSample);
               }
            }

            // create the uv options
            if(mUvOptionsVec.size() == 0)
            {
               mUvOptionsProperty = OFloatArrayProperty(mMeshSchema, ".uvOptions", mMeshSchema.getMetaData(), GetJob()->GetAnimatedTs() );

               for(LONG uvI=0;uvI<uvPropRefs.GetCount();uvI++)
               {
                 ClusterProperty clusterProperty = (ClusterProperty) uvPropRefs[uvI];
				  bool subdsmooth = false;
				  if( clusterProperty.GetType() == L"uvspace") {
				      subdsmooth = (bool)clusterProperty.GetParameter(L"subdsmooth").GetValue();      
					  //ESS_LOG_ERROR( "subdsmooth: " << subdsmooth );
				  }

                  CRefArray children = clusterProperty.GetNestedObjects();
                  bool uWrap = false;
                  bool vWrap = false;
                  for(LONG i=0; i<children.GetCount(); i++)
                  {
                     ProjectItem child(children.GetItem(i));
                     CString type = child.GetType();
					// ESS_LOG_ERROR( "  Cluster Property child type: " << type.GetAsciiString() );
                     if(type == L"uvprojdef")
                     {
                        uWrap = (bool)child.GetParameter(L"wrap_u").GetValue();
                        vWrap = (bool)child.GetParameter(L"wrap_v").GetValue();
                        break;
                     }
                  }

                  // uv wrapping
                  mUvOptionsVec.push_back(uWrap ? 1.0f : 0.0f);
                  mUvOptionsVec.push_back(vWrap ? 1.0f : 0.0f);
				  mUvOptionsVec.push_back(subdsmooth ? 1.0f : 0.0f);
               }
               mUvOptionsProperty.set(Alembic::Abc::FloatArraySample(&mUvOptionsVec.front(),mUvOptionsVec.size()));
            }
         }
      }

      // sweet, now let's have a look at face sets (really only for first sample)
      if(GetJob()->GetOption(L"exportFaceSets") && mNumSamples == 0)
      {
         for(LONG i=0;i<clusters.GetCount();i++)
         {
            Cluster cluster(clusters[i]);
            if(!cluster.GetType().IsEqualNoCase(L"poly"))
               continue;

            CLongArray elements = cluster.GetElements().GetArray();
            if(elements.GetCount() == 0)
               continue;

            std::string name(cluster.GetName().GetAsciiString());

            mFaceSetsVec.push_back(std::vector<int32_t>());
            std::vector<int32_t> & faceSetVec = mFaceSetsVec.back();
            for(LONG j=0;j<elements.GetCount();j++)
               faceSetVec.push_back(elements[j]);

            if(faceSetVec.size() > 0)
            {
               Alembic::AbcGeom::OFaceSet faceSet = mMeshSchema.createFaceSet(name);
               Alembic::AbcGeom::OFaceSetSchema::Sample faceSetSample(Alembic::Abc::Int32ArraySample(&faceSetVec.front(),faceSetVec.size()));
               faceSet.getSchema().set(faceSetSample);
            }
         }
      }

      // save the sample
      mMeshSchema.set(mMeshSample);

      // check if we need to export the bindpose (also only for first frame)
      if(GetJob()->GetOption(L"exportBindPose") && prim.GetParent3DObject().GetEnvelopes().GetCount() > 0 && mNumSamples == 0)
      {
         mBindPoseProperty = OV3fArrayProperty(mMeshSchema, ".bindpose", mMeshSchema.getMetaData(), GetJob()->GetAnimatedTs());

         // store the positions of the modeling stack into here
         PolygonMesh bindPoseGeo = prim.GetGeometry(time, siConstructionModeModeling);
         CVector3Array bindPosePos = bindPoseGeo.GetPoints().GetPositionArray();
         mBindPoseVec.resize((size_t)bindPosePos.GetCount());
         for(LONG i=0;i<bindPosePos.GetCount();i++)
         {
            mBindPoseVec[i].x = (float)bindPosePos[i].GetX();
            mBindPoseVec[i].y = (float)bindPosePos[i].GetY();
            mBindPoseVec[i].z = (float)bindPosePos[i].GetZ();
         }

         Alembic::Abc::V3fArraySample sample;
         if(mBindPoseVec.size() > 0)
            sample = Alembic::Abc::V3fArraySample(&mBindPoseVec.front(),mBindPoseVec.size());
         mBindPoseProperty.set(sample);
      }   
   }
   else
   {
      Alembic::AbcGeom::ON3fGeomParam::Sample normalSample;
      if(normalVec.size() > 0 && normalCount > 0)
      {
         normalSample.setScope(Alembic::AbcGeom::kFacevaryingScope);
         normalSample.setVals(Alembic::Abc::N3fArraySample(&normalVec.front(),normalCount));
         if(normalIndexCount > 0)
            normalSample.setIndices(Alembic::Abc::UInt32ArraySample(&normalIndexVec.front(),normalIndexCount));
         mMeshSample.setNormals(normalSample);
      }
      mMeshSchema.set(mMeshSample);
   }

   mNumSamples++;

   return CStatus::OK;
}

ESS_CALLBACK_START( alembic_polymesh_Define, CRef& )
   return alembicOp_Define(in_ctxt);
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_polymesh_DefineLayout, CRef& )
   return alembicOp_DefineLayout(in_ctxt); 
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_polymesh_Update, CRef& )
   ESS_PROFILE_SCOPE("alembic_polymesh_Update");
   OperatorContext ctxt( in_ctxt );

   if((bool)ctxt.GetParameterValue(L"muted"))
      return CStatus::OK;

   CString path = ctxt.GetParameterValue(L"path");
   CString identifier = ctxt.GetParameterValue(L"identifier");

   Alembic::AbcGeom::IObject iObj = getObjectFromArchive(path,identifier);
   if(!iObj.valid())
      return CStatus::OK;
   Alembic::AbcGeom::IPolyMesh objMesh;
   Alembic::AbcGeom::ISubD objSubD;
   if(Alembic::AbcGeom::IPolyMesh::matches(iObj.getMetaData()))
      objMesh = Alembic::AbcGeom::IPolyMesh(iObj,Alembic::Abc::kWrapExisting);
   else
      objSubD = Alembic::AbcGeom::ISubD(iObj,Alembic::Abc::kWrapExisting);
   if(!objMesh.valid() && !objSubD.valid())
      return CStatus::OK;

   SampleInfo sampleInfo;
   if(objMesh.valid())
      sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         objMesh.getSchema().getTimeSampling(),
         objMesh.getSchema().getNumSamples()
      );
   else
      sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         objSubD.getSchema().getTimeSampling(),
         objSubD.getSchema().getNumSamples()
      );

   Alembic::Abc::P3fArraySamplePtr meshPos;
   if(objMesh.valid())
   {
      Alembic::AbcGeom::IPolyMeshSchema::Sample sample;
      objMesh.getSchema().get(sample,sampleInfo.floorIndex);
      meshPos = sample.getPositions();
   }
   else
   {
      Alembic::AbcGeom::ISubDSchema::Sample sample;
      objSubD.getSchema().get(sample,sampleInfo.floorIndex);
      meshPos = sample.getPositions();
   }

   PolygonMesh inMesh = Primitive((CRef)ctxt.GetInputValue(0)).GetGeometry();
   CVector3Array pos = inMesh.GetPoints().GetPositionArray();

   if(pos.GetCount() != meshPos->size())
      return CStatus::OK;

   for(size_t i=0;i<meshPos->size();i++)
      pos[(LONG)i].Set(meshPos->get()[i].x,meshPos->get()[i].y,meshPos->get()[i].z);

   // blend
   if(sampleInfo.alpha != 0.0)
   {
      if(objMesh.valid())
      {
         Alembic::AbcGeom::IPolyMeshSchema::Sample sample;
         objMesh.getSchema().get(sample,sampleInfo.ceilIndex);
         meshPos = sample.getPositions();
      }
      else
      {
         Alembic::AbcGeom::ISubDSchema::Sample sample;
         objSubD.getSchema().get(sample,sampleInfo.ceilIndex);
         meshPos = sample.getPositions();
      }
      for(size_t i=0;i<meshPos->size();i++)
         pos[(LONG)i].LinearlyInterpolate(pos[(LONG)i],CVector3(meshPos->get()[i].x,meshPos->get()[i].y,meshPos->get()[i].z),sampleInfo.alpha);
   }

   Primitive(ctxt.GetOutputTarget()).GetGeometry().GetPoints().PutPositionArray(pos);

   return CStatus::OK;
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_polymesh_Term, CRef& )
   return alembicOp_Term(in_ctxt);
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_normals_Define, CRef& )
   return alembicOp_Define(in_ctxt);
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_normals_DefineLayout, CRef& )
   return alembicOp_DefineLayout(in_ctxt);
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_normals_Update, CRef& )
   ESS_PROFILE_SCOPE("alembic_normals_Update");
   OperatorContext ctxt( in_ctxt );

   if((bool)ctxt.GetParameterValue(L"muted"))
      return CStatus::OK;

   CString path = ctxt.GetParameterValue(L"path");
   CString identifier = ctxt.GetParameterValue(L"identifier");

   Alembic::AbcGeom::IObject iObj = getObjectFromArchive(path,identifier);
   if(!iObj.valid())
      return CStatus::OK;
   Alembic::AbcGeom::IPolyMesh obj(iObj,Alembic::Abc::kWrapExisting);
   if(!obj.valid())
      return CStatus::OK;

   SampleInfo sampleInfo = getSampleInfo(
      ctxt.GetParameterValue(L"time"),
      obj.getSchema().getTimeSampling(),
      obj.getSchema().getNumSamples()
   );

   CDoubleArray normalValues = ClusterProperty((CRef)ctxt.GetInputValue(0)).GetElements().GetArray();
   PolygonMesh mesh = Primitive((CRef)ctxt.GetInputValue(1)).GetGeometry();
   CGeometryAccessor accessor = mesh.GetGeometryAccessor(siConstructionModeModeling);
   CLongArray counts;
   accessor.GetPolygonVerticesCount(counts);

   Alembic::AbcGeom::IN3fGeomParam meshNormalsParam = obj.getSchema().getNormalsParam();
   if(meshNormalsParam.valid())
   {
      Alembic::Abc::N3fArraySamplePtr meshNormals = meshNormalsParam.getExpandedValue(sampleInfo.floorIndex).getVals();
      if(meshNormals->size() * 3 == normalValues.GetCount())
      {
         // let's apply it!
         LONG offsetIn = 0;
         LONG offsetOut = 0;
         for(LONG i=0;i<counts.GetCount();i++)
         {
            for(LONG j=counts[i]-1;j>=0;j--)
            {
               normalValues[offsetOut++] = meshNormals->get()[offsetIn+j].x;
               normalValues[offsetOut++] = meshNormals->get()[offsetIn+j].y;
               normalValues[offsetOut++] = meshNormals->get()[offsetIn+j].z;
            }
            offsetIn += counts[i];
         }

         // blend
         if(sampleInfo.alpha != 0.0)
         {
            meshNormals = meshNormalsParam.getExpandedValue(sampleInfo.ceilIndex).getVals();
            if(meshNormals->size() == normalValues.GetCount() / 3)
            {
               offsetIn = 0;
               offsetOut = 0;

               for(LONG i=0;i<counts.GetCount();i++)
               {
                  for(LONG j=counts[i]-1;j>=0;j--)
                  {
                     CVector3 normal(normalValues[offsetOut],normalValues[offsetOut+1],normalValues[offsetOut+2]);
                     normal.LinearlyInterpolate(normal,CVector3(
                        meshNormals->get()[offsetIn+j].x,
                        meshNormals->get()[offsetIn+j].y,
                        meshNormals->get()[offsetIn+j].z),sampleInfo.alpha);
                     normal.NormalizeInPlace();
                     normalValues[offsetOut++] = normal.GetX();
                     normalValues[offsetOut++] = normal.GetY();
                     normalValues[offsetOut++] = normal.GetZ();
                  }
                  offsetIn += counts[i];
               }
            }
         }
      }
   }

   ClusterProperty(ctxt.GetOutputTarget()).GetElements().PutArray(normalValues);

   return CStatus::OK;
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_normals_Term, CRef& )
   Context ctxt( in_ctxt );
   CustomOperator op(ctxt.GetSource());
   delRefArchive(op.GetParameterValue(L"path").GetAsText());
   return CStatus::OK;
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_uvs_Define, CRef& )
   return alembicOp_Define(in_ctxt);
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_uvs_DefineLayout, CRef& )
   return alembicOp_DefineLayout(in_ctxt);
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_uvs_Update, CRef& )
   ESS_PROFILE_SCOPE("alembic_uvs_Update");
   OperatorContext ctxt( in_ctxt );

   if((bool)ctxt.GetParameterValue(L"muted"))
      return CStatus::OK;

   CString path = ctxt.GetParameterValue(L"path");
   CStringArray identifierAndIndex = CString(ctxt.GetParameterValue(L"identifier")).Split(L":");
   CString identifier = identifierAndIndex[0];
   LONG uvI = 0;
   if(identifierAndIndex.GetCount() > 1)
      uvI = (LONG)CValue(identifierAndIndex[1]);

   Alembic::AbcGeom::IObject iObj = getObjectFromArchive(path,identifier);
   if(!iObj.valid())
      return CStatus::OK;
   Alembic::AbcGeom::IPolyMesh objMesh;
   Alembic::AbcGeom::ISubD objSubD;
   if(Alembic::AbcGeom::IPolyMesh::matches(iObj.getMetaData()))
      objMesh = Alembic::AbcGeom::IPolyMesh(iObj,Alembic::Abc::kWrapExisting);
   else
      objSubD = Alembic::AbcGeom::ISubD(iObj,Alembic::Abc::kWrapExisting);
   if(!objMesh.valid() && !objSubD.valid())
      return CStatus::OK;

   CDoubleArray uvValues = ClusterProperty((CRef)ctxt.GetInputValue(0)).GetElements().GetArray();
   PolygonMesh mesh = Primitive((CRef)ctxt.GetInputValue(1)).GetGeometry();
   CPolygonFaceRefArray faces = mesh.GetPolygons();
   CGeometryAccessor accessor = mesh.GetGeometryAccessor(siConstructionModeModeling);
   CLongArray counts;
   accessor.GetPolygonVerticesCount(counts);

   Alembic::AbcGeom::IV2fGeomParam meshUvParam;
   if(objMesh.valid())
   {
      if(uvI == 0)
         meshUvParam = objMesh.getSchema().getUVsParam();
      else
      {
         CString storedUVName = CString(L"uv")+CString(uvI);
         if(objMesh.getSchema().getPropertyHeader( storedUVName.GetAsciiString() ) == NULL)
            return CStatus::OK;
         meshUvParam = Alembic::AbcGeom::IV2fGeomParam( objMesh.getSchema(), storedUVName.GetAsciiString());
      }
   }
   else
   {
      if(uvI == 0)
         meshUvParam = objSubD.getSchema().getUVsParam();
      else
      {
         CString storedUVName = CString(L"uv")+CString(uvI);
         if(objSubD.getSchema().getPropertyHeader( storedUVName.GetAsciiString() ) == NULL)
            return CStatus::OK;
         meshUvParam = Alembic::AbcGeom::IV2fGeomParam( objSubD.getSchema(), storedUVName.GetAsciiString());
      }
   }

   if(meshUvParam.valid())
   {
      SampleInfo sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         meshUvParam.getTimeSampling(),
         meshUvParam.getNumSamples()
      );

      Alembic::Abc::V2fArraySamplePtr meshUVs = meshUvParam.getExpandedValue(sampleInfo.floorIndex).getVals();
      if(meshUVs->size() * 3 == uvValues.GetCount())
      {
         // create a sample look table
         LONG offset = 0;
         CLongArray sampleLookup(accessor.GetNodeCount());
         for(LONG i=0;i<faces.GetCount();i++)
         {
            PolygonFace face(faces[i]);
            CLongArray samples = face.GetSamples().GetIndexArray();
            for(LONG j=samples.GetCount()-1;j>=0;j--)
               sampleLookup[samples[j]] = offset++;
               //sampleLookup[offset++] = samples[j];
         }

         // let's apply it!
         offset = 0;
         for(LONG i=0;i<sampleLookup.GetCount();i++)
         {
            uvValues[offset++] = meshUVs->get()[sampleLookup[i]].x;
            uvValues[offset++] = meshUVs->get()[sampleLookup[i]].y;
            uvValues[offset++] = 0.0;
         }

         if(sampleInfo.alpha != 0.0)
         {
            meshUVs = meshUvParam.getExpandedValue(sampleInfo.ceilIndex).getVals();
            double ialpha = 1.0 - sampleInfo.alpha;

            offset = 0;
            for(LONG i=0;i<sampleLookup.GetCount();i++)
            {
               uvValues[offset++] = uvValues[offset] * ialpha + meshUVs->get()[sampleLookup[i]].x * sampleInfo.alpha;;
               uvValues[offset++] = uvValues[offset] * ialpha + meshUVs->get()[sampleLookup[i]].y * sampleInfo.alpha;;
               uvValues[offset++] = 0.0;
            }
         }
      }
   }

   ClusterProperty(ctxt.GetOutputTarget()).GetElements().PutArray(uvValues);

   return CStatus::OK;
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_uvs_Term, CRef& )
   return alembicOp_Term(in_ctxt);
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_polymesh_topo_Define, CRef& )
   return alembicOp_Define(in_ctxt);
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_polymesh_topo_DefineLayout, CRef& )
   return alembicOp_DefineLayout(in_ctxt);
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_polymesh_topo_Update, CRef& )
   ESS_PROFILE_SCOPE("alembic_polymesh_topo_Update");
   OperatorContext ctxt( in_ctxt );

   //ESS_LOG_INFO("ENTER alembic_polymesh_topo_Update");

   if((bool)ctxt.GetParameterValue(L"muted"))
      return CStatus::OK;

   CString path = ctxt.GetParameterValue(L"path");
   CString identifier = ctxt.GetParameterValue(L"identifier");

   Alembic::AbcGeom::IObject iObj = getObjectFromArchive(path,identifier);
   if(!iObj.valid())
      return CStatus::OK;
   Alembic::AbcGeom::IPolyMesh objMesh;
   Alembic::AbcGeom::ISubD objSubD;
   if(Alembic::AbcGeom::IPolyMesh::matches(iObj.getMetaData()))
      objMesh = Alembic::AbcGeom::IPolyMesh(iObj,Alembic::Abc::kWrapExisting);
   else
      objSubD = Alembic::AbcGeom::ISubD(iObj,Alembic::Abc::kWrapExisting);
   if(!objMesh.valid() && !objSubD.valid())
      return CStatus::OK;

   SampleInfo sampleInfo;
   if(objMesh.valid())
   {
      sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         objMesh.getSchema().getTimeSampling(),
         objMesh.getSchema().getNumSamples()
      );
   }
   else
   {
      sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         objSubD.getSchema().getTimeSampling(),
         objSubD.getSchema().getNumSamples()
      );
   }

   Alembic::Abc::P3fArraySamplePtr meshPos;
   Alembic::Abc::V3fArraySamplePtr meshVel;
   Alembic::Abc::Int32ArraySamplePtr meshFaceCount;
   Alembic::Abc::Int32ArraySamplePtr meshFaceIndices;

   bool hasDynamicTopo = isAlembicMeshTopoDynamic( & objMesh );
   if(objMesh.valid())
   {
      Alembic::AbcGeom::IPolyMeshSchema::Sample sample;
      objMesh.getSchema().get(sample,sampleInfo.floorIndex);
      meshPos = sample.getPositions();
      meshVel = sample.getVelocities();
      meshFaceCount = sample.getFaceCounts();
      meshFaceIndices = sample.getFaceIndices();
   }
   else
   {
      Alembic::AbcGeom::ISubDSchema::Sample sample;
      objSubD.getSchema().get(sample,sampleInfo.floorIndex);
      meshPos = sample.getPositions();
      meshVel = sample.getVelocities();
      meshFaceCount = sample.getFaceCounts();
      meshFaceIndices = sample.getFaceIndices();
   }

   CVector3Array pos((LONG)meshPos->size());
   CLongArray polies((LONG)(meshFaceCount->size() + meshFaceIndices->size()));

   for(size_t j=0;j<meshPos->size();j++)
      pos[(LONG)j].Set(meshPos->get()[j].x,meshPos->get()[j].y,meshPos->get()[j].z);

   // check if this is an empty topo object
   if(meshFaceCount->size() > 0)
   {
      if(meshFaceCount->get()[0] == 0)
      {
         if(!meshVel)
            return CStatus::OK;
         if(meshVel->size() != meshPos->size())
            return CStatus::OK;

         // dummy topo
         polies.Resize(4);
         polies[0] = 3;
         polies[1] = 0;
         polies[2] = 0;
         polies[3] = 0;
      }
      else
      {

         LONG offset1 = 0;
         Alembic::Abc::int32_t offset2 = 0;

         ESS_LOG_INFO("face count: " << (unsigned int)meshFaceCount->size());

         for(size_t j=0;j<meshFaceCount->size();j++)
         {
            Alembic::Abc::int32_t singleFaceCount = meshFaceCount->get()[j];
            polies[offset1++] = singleFaceCount;
            offset2 += singleFaceCount;

            ESS_LOG_INFO("singleFaceCount: " << (unsigned int)singleFaceCount);
            ESS_LOG_INFO("offset2: " << (unsigned int)offset2);
            ESS_LOG_INFO("meshFaceIndices->size(): " << (unsigned int)meshFaceIndices->size());

            unsigned int meshFIndxSz = (unsigned int)meshFaceIndices->size();

            for(size_t k=0;k<singleFaceCount;k++)
            {
               ESS_LOG_INFO("index: " << (unsigned int)((size_t)offset2 - 1 - k));
               polies[offset1++] = meshFaceIndices->get()[(size_t)offset2 - 1 - k];
            }
         }
      }
   }

   // do the positional interpolation if necessary
   if(sampleInfo.alpha != 0.0)
   {
      double alpha = sampleInfo.alpha;
      double ialpha = 1.0 - alpha;

      // first check if the next frame has the same point count
      if(objMesh.valid())
      {
         Alembic::AbcGeom::IPolyMeshSchema::Sample sample;
         objMesh.getSchema().get(sample,sampleInfo.ceilIndex);
         meshPos = sample.getPositions();
      }
      else
      {
         Alembic::AbcGeom::ISubDSchema::Sample sample;
         objSubD.getSchema().get(sample,sampleInfo.floorIndex);
         meshPos = sample.getPositions();
      }

      if(meshPos->size() == (size_t)pos.GetCount() && !hasDynamicTopo)
      {
         for(LONG i=0;i<(LONG)meshPos->size();i++)
         {
            pos[i].PutX(ialpha * pos[i].GetX() + alpha * meshPos->get()[i].x);
            pos[i].PutY(ialpha * pos[i].GetY() + alpha * meshPos->get()[i].y);
            pos[i].PutZ(ialpha * pos[i].GetZ() + alpha * meshPos->get()[i].z);
         }
      }
      else if(meshVel)
      {
         double timeAlpha = (double)(objMesh.getSchema().getTimeSampling()->getSampleTime(sampleInfo.ceilIndex) - 
                            objMesh.getSchema().getTimeSampling()->getSampleTime(sampleInfo.floorIndex)) * alpha;
         if(meshVel->size() == (size_t)pos.GetCount())
         {
            for(LONG i=0;i<(LONG)meshVel->size();i++)
            {
               pos[i].PutX(pos[i].GetX() + timeAlpha * meshVel->get()[i].x);
               pos[i].PutY(pos[i].GetY() + timeAlpha * meshVel->get()[i].y);
               pos[i].PutZ(pos[i].GetZ() + timeAlpha * meshVel->get()[i].z);
            }
         }
      }
   }

   PolygonMesh outMesh = Primitive(ctxt.GetOutputTarget()).GetGeometry();
   outMesh.Set(pos,polies);

   //ESS_LOG_INFO("EXIT alembic_polymesh_topo_Update");
   return CStatus::OK;
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_polymesh_topo_Term, CRef& )
   return alembicOp_Term(in_ctxt);
ESS_CALLBACK_END

ESS_CALLBACK_START( alembic_bbox_Define, CRef& )
   alembicOp_Define(in_ctxt);

   Context ctxt( in_ctxt );
   CustomOperator oCustomOperator;

   Parameter oParam;
   CRef oPDef;

   Factory oFactory = Application().GetFactory();
   oCustomOperator = ctxt.GetSource();

   oPDef = oFactory.CreateParamDef(L"extend",CValue::siFloat,siAnimatable| siPersistable,L"extend",L"extend",0.0f,-10000.0f,10000.0f,0.0f,10.0f);
   oCustomOperator.AddParameter(oPDef,oParam);
   return CStatus::OK;
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_bbox_DefineLayout, CRef& )
   alembicOp_DefineLayout(in_ctxt);

   Context ctxt( in_ctxt );
   PPGLayout oLayout;
   PPGItem oItem;
   oLayout = ctxt.GetSource();
   oLayout.AddItem(L"extend",L"Extend Box");
   return CStatus::OK;
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_bbox_Update, CRef& )
   OperatorContext ctxt( in_ctxt );

   if((bool)ctxt.GetParameterValue(L"muted"))
      return CStatus::OK;

   CString path = ctxt.GetParameterValue(L"path");
   CString identifier = ctxt.GetParameterValue(L"identifier");
   float extend = ctxt.GetParameterValue(L"extend");

   Alembic::AbcGeom::IObject iObj = getObjectFromArchive(path,identifier);
   if(!iObj.valid())
      return CStatus::OK;

   Alembic::Abc::Box3d box;

   
   // check what kind of object we have
   const Alembic::Abc::MetaData &md = iObj.getMetaData();
   if(Alembic::AbcGeom::IPolyMesh::matches(md)) {
      Alembic::AbcGeom::IPolyMesh obj(iObj,Alembic::Abc::kWrapExisting);
      if(!obj.valid())
         return CStatus::OK;

      SampleInfo sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         obj.getSchema().getTimeSampling(),
         obj.getSchema().getNumSamples()
      );

      Alembic::AbcGeom::IPolyMeshSchema::Sample sample;
      obj.getSchema().get(sample,sampleInfo.floorIndex);
      box = sample.getSelfBounds();

      if(sampleInfo.alpha > 0.0)
      {
         obj.getSchema().get(sample,sampleInfo.ceilIndex);
         Alembic::Abc::Box3d box2 = sample.getSelfBounds();

         box.min = (1.0 - sampleInfo.alpha) * box.min + sampleInfo.alpha * box2.min;
         box.max = (1.0 - sampleInfo.alpha) * box.max + sampleInfo.alpha * box2.max;
      }
   } else if(Alembic::AbcGeom::ICurves::matches(md)) {
      Alembic::AbcGeom::ICurves obj(iObj,Alembic::Abc::kWrapExisting);
      if(!obj.valid())
         return CStatus::OK;

      SampleInfo sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         obj.getSchema().getTimeSampling(),
         obj.getSchema().getNumSamples()
      );

      Alembic::AbcGeom::ICurvesSchema::Sample sample;
      obj.getSchema().get(sample,sampleInfo.floorIndex);
      box = sample.getSelfBounds();

      if(sampleInfo.alpha > 0.0)
      {
         obj.getSchema().get(sample,sampleInfo.ceilIndex);
         Alembic::Abc::Box3d box2 = sample.getSelfBounds();

         box.min = (1.0 - sampleInfo.alpha) * box.min + sampleInfo.alpha * box2.min;
         box.max = (1.0 - sampleInfo.alpha) * box.max + sampleInfo.alpha * box2.max;
      }
   } else if(Alembic::AbcGeom::IPoints::matches(md)) {
      Alembic::AbcGeom::IPoints obj(iObj,Alembic::Abc::kWrapExisting);
      if(!obj.valid())
         return CStatus::OK;

      SampleInfo sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         obj.getSchema().getTimeSampling(),
         obj.getSchema().getNumSamples()
      );

      Alembic::AbcGeom::IPointsSchema::Sample sample;
      obj.getSchema().get(sample,sampleInfo.floorIndex);
      box = sample.getSelfBounds();

      if(sampleInfo.alpha > 0.0)
      {
         obj.getSchema().get(sample,sampleInfo.ceilIndex);
         Alembic::Abc::Box3d box2 = sample.getSelfBounds();

         box.min = (1.0 - sampleInfo.alpha) * box.min + sampleInfo.alpha * box2.min;
         box.max = (1.0 - sampleInfo.alpha) * box.max + sampleInfo.alpha * box2.max;
      }
   } else if(Alembic::AbcGeom::ISubD::matches(md)) {
      Alembic::AbcGeom::ISubD obj(iObj,Alembic::Abc::kWrapExisting);
      if(!obj.valid())
         return CStatus::OK;

      SampleInfo sampleInfo = getSampleInfo(
         ctxt.GetParameterValue(L"time"),
         obj.getSchema().getTimeSampling(),
         obj.getSchema().getNumSamples()
      );

      Alembic::AbcGeom::ISubDSchema::Sample sample;
      obj.getSchema().get(sample,sampleInfo.floorIndex);
      box = sample.getSelfBounds();

      if(sampleInfo.alpha > 0.0)
      {
         obj.getSchema().get(sample,sampleInfo.ceilIndex);
         Alembic::Abc::Box3d box2 = sample.getSelfBounds();

         box.min = (1.0 - sampleInfo.alpha) * box.min + sampleInfo.alpha * box2.min;
         box.max = (1.0 - sampleInfo.alpha) * box.max + sampleInfo.alpha * box2.max;
      }
   }

   Primitive inPrim((CRef)ctxt.GetInputValue(0));
   CVector3Array pos = inPrim.GetGeometry().GetPoints().GetPositionArray();

   box.min.x -= extend;
   box.min.y -= extend;
   box.min.z -= extend;
   box.max.x += extend;
   box.max.y += extend;
   box.max.z += extend;

   // apply the bbox
   for(LONG i=0;i<pos.GetCount();i++)
   {
      pos[i].PutX( pos[i].GetX() < 0 ? box.min.x : box.max.x );
      pos[i].PutY( pos[i].GetY() < 0 ? box.min.y : box.max.y );
      pos[i].PutZ( pos[i].GetZ() < 0 ? box.min.z : box.max.z );
   }

   Primitive outPrim(ctxt.GetOutputTarget());
   outPrim.GetGeometry().GetPoints().PutPositionArray(pos);

   return CStatus::OK;
ESS_CALLBACK_END


ESS_CALLBACK_START( alembic_bbox_Term, CRef& )
   return alembicOp_Term(in_ctxt);
ESS_CALLBACK_END
