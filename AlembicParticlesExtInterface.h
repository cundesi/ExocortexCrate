#include "ExocortexCoreServicesAPI.h"
#include <IParticleObjectExt.h>

class AlembicParticles;

class IAlembicParticlesExt : public IParticleObjectExt
{
	AlembicParticles* m_pAlembicParticles;

public:

	IAlembicParticlesExt(AlembicParticles* pAlembicParticles);

	// Implemented by the Plug-In.
	// Since particles may have different motion, the particle system should supply speed information
	// on per vertex basis for a motion blur effect to be able to generate effect.
	// the method is not exposed in maxscript
	// returns true if the object supports the method
	// Parameters:
	//		TimeValue t
	//			The time to get the mesh vertices speed.
	//		INode *inode
	//			The node in the scene
	//		View& view
	//			If the renderer calls this method it will pass the view information here.
	//		Tab<Point3>& speed
	//			speed per vertex in world coordinates
	virtual bool GetRenderMeshVertexSpeed(TimeValue t, INode *inode, View& view, Tab<Point3>& speed); 

	// Implemented by the Plug-In.
	// Particle system may supply multiple render meshes. If this method returns a positive number, 
	// then GetMultipleRenderMesh and GetMultipleRenderMeshTM will be called for each mesh, 
	// instead of calling GetRenderMesh. The method has a current time parameter which is not
	// the case with the NumberOfRenderMeshes method of GeomObject class
	// the method is not exposed in maxscript
	// Parameters:
	//		TimeValue t
	//			Time for the number of render meshes request.
	//		INode *inode
	//			The node in the scene
	//		View& view
	//			If the renderer calls this method it will pass the view information here.
	virtual int NumberOfRenderMeshes(TimeValue t, INode *inode, View& view);
	
	// For multiple render meshes, if it supports vertex speed for motion blur, this method must be implemented
	// Since particles may have different motion, the particle system should supply speed information
	// on per vertex basis for a motion blur effect to be able to generate effect.
	// the method is not exposed in maxscript
	// returns true if the particular render mesh supports the method
	// Parameters:
	//		TimeValue t
	//			The time to get the mesh vertices speed.
	//		INode *inode
	//			The node in the scene
	//		View& view
	//			If the renderer calls this method it will pass the view information here.
	//		int meshNumber
	//			Specifies which of the multiple meshes is being asked for.
	//		Tab<Point3>& speed
	//			speed per vertex in world coordinates
    virtual	bool GetMultipleRenderMeshVertexSpeed(TimeValue t, INode *inode, View& view, int meshNumber, Tab<Point3>& speed) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetMultipleRenderMeshVertexSpeed not implmented.");
		return false; 
	}

	// Implemented by the Plug-In.
	// This method is called so the particle system can update its state to reflect 
	// the current time passed.  This may involve generating new particle that are born, 
	// eliminating old particles that have expired, computing the impact of collisions or 
	// force field effects, and modify properties of the particles.
	// Parameters:
	//		TimeValue t
	//			The particles should be updated to reflect this time.
	//		INode *node
	//			This is the emitter node.
	// the method is not exposed in maxscript
	virtual void UpdateParticles(INode *node, TimeValue t) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::UpdateParticles not implmented."); 
	}

	// Implemented by the Plug-in
	// Use this method to retrieve time of the current update step. The update time maybe unrelated to 
	// the current time of the scene.
	virtual TimeValue GetUpdateTime() { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetUpdateTime not implmented.");
		return 0; 
	}
	// Implemented by the Plug-in
	// Use this method to retrieve time interval of the current update step. The update time maybe unrelated to 
	// the current time of the scene. The GetUpdateTime method above retrieves the finish time.
	virtual void GetUpdateInterval(TimeValue& start, TimeValue& finish) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetUpdateInterval not implmented."); 
	}

	// Implemented by the Plug-In.
	// The method returns how many particles are currently in the particle system. 
	// Some of these particles may be dead or not born yet (indicated by GetAge(..) method =-1). 
	virtual int NumParticles();

	// Implemented by the Plug-In.
	// The method returns how many particles were born. Since particle systems have
	// a tendency of reusing indices for newly born particles, sometimes it's necessary 
	// to keep a track for particular particles. This method and the methods that deal with
	// particle IDs allow us to accomplish that.
	virtual int NumParticlesGenerated() { return NumParticles(); }

	// Implemented by the Plug-in
	// The following four methods modify amount of particles in the particle system
	// Returns true if the operation was completed successfully
	//		Add a single particle
	virtual bool AddParticle() { return false; }
	//		Add "num" particles into the particle system
	virtual bool AddParticles(int num) { return false; }
	//		Delete a single particle with the given index
	virtual bool DeleteParticle(int index) { return false; }
	//		List-type delete of "num" particles starting with "start"
	virtual bool DeleteParticles(int start, int num) { return false; }

	// Implemented by the Plug-In.
	// Each particle is given a unique ID (consecutive) upon its birth. The method 
	// allows us to distinguish physically different particles even if they are using 
	// the same particle index (because of the "index reusing").
	// Parameters:
	//		int i
	//			index of the particle in the range of [0, NumParticles-1]
	virtual int GetParticleBornIndex(int i) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleBornIndex not implmented.");
		return 0; 
	}

	// Implemented by the Plug-In.
	// the methods verifies if a particle with a given particle id (born index) is present
	// in the particle system. The methods returns Particle Group node the particle belongs to,
	// and index in this group. If there is no such particle, the method returns false.
	// Parameters:
	//		int bornIndex
	//			particle born index
	//		INode*& groupNode
	//			particle group the particle belongs to
	//		int index
	//			particle index in the particle group or particle system
	virtual bool HasParticleBornIndex(int bornIndex, int& index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::HasParticleBornIndex not implmented.");
		return false; 
	}
	virtual INode* GetParticleGroup(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleGroup not implmented.");
		return NULL; 
	}
	virtual int GetParticleIndex(int bornIndex) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleIndex not implmented.");
		return 0; 
	}

	// Implemented by the Plug-In.
	// The following four methods define "current" index or bornIndex. This index is used
	// in the property methods below to get the property without specifying the index.
	virtual int GetCurrentParticleIndex() { 
		return 0; 
	}
	virtual int GetCurrentParticleBornIndex() { return 5; }
	virtual void SetCurrentParticleIndex(int index) { ; }
	virtual void SetCurrentParticleBornIndex(int bornIndex) { ; }

	// Implemented by the Plug-In.
	// The following six methods define age of the specified particle. Particle is specified by either its
	// index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		TimeValue age
	//			new age value to set for a particle
	virtual TimeValue GetParticleAgeByIndex(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleAgeByIndex not implmented.");
		return 0; 
	}
	virtual TimeValue GetParticleAgeByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleAgeByBornIndex not implmented.");
		return 0; 
	}
	virtual void SetParticleAgeByIndex(int index, TimeValue age) { ; }
	virtual void SetParticleAgeByBornIndex(int id, TimeValue age) { ; }
	virtual TimeValue GetParticleAge() { return 0; }
	virtual void SetParticleAge(TimeValue age) { ; }

	// Implemented by the Plug-In.
	// The following six methods define lifespan of the specified particle. Particle is specified by either its
	// index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		TimeValue lifespan
	//			new lifespan value to set for a particle
	virtual TimeValue GetParticleLifeSpanByIndex(int index) { return TIME_PosInfinity; }
	virtual TimeValue GetParticleLifeSpanByBornIndex(int id) { return TIME_PosInfinity; }
	virtual void SetParticleLifeSpanByIndex(int index, TimeValue LifeSpan) { ; }
	virtual void SetParticleLifeSpanByBornIndex(int id, TimeValue LifeSpan) { ; }
	virtual TimeValue GetParticleLifeSpan() { return TIME_PosInfinity; }
	virtual void SetParticleLifeSpan(TimeValue lifespan) { ; }

	// Implemented by the Plug-In.
	// The following six methods define for how long the specified particle was staying in the current
	// particle group. Particle is specified by either its
	// index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		TimeValue time
	//			how long particle was staying in the current particle group
	virtual TimeValue GetParticleGroupTimeByIndex(int index) { return 0; }
	virtual TimeValue GetParticleGroupTimeByBornIndex(int id) { return 0; }
	virtual void SetParticleGroupTimeByIndex(int index, TimeValue time) { ; }
	virtual void SetParticleGroupTimeByBornIndex(int id, TimeValue time) { ; }
	virtual TimeValue GetParticleGroupTime() { return 0; }
	virtual void SetParticleGroupTime(TimeValue time) { ; }
	
	// Implemented by the Plug-In.
	// The following six methods define position of the specified particle in the current state.
	// Particle is specified by either its index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		Point3 pos
	//			position of the particle
	virtual Point3* GetParticlePositionByIndex(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticlePositionByIndex not implmented.");
		return NULL; 
	}
	virtual Point3* GetParticlePositionByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticlePositionByBornIndex not implmented.");
		return NULL; 
	}
	virtual void SetParticlePositionByIndex(int index, Point3 pos) { ; }
	virtual void SetParticlePositionByBornIndex(int id, Point3 pos) { ; }
	virtual Point3* GetParticlePosition() { return NULL; }
	virtual void SetParticlePosition(Point3 pos) { ; }

	// Implemented by the Plug-In.
	// The following six methods define speed of the specified particle in the current state.
	// Particle is specified by either its index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		Point3 speed
	//			speed of the particle in units per frame
	virtual Point3* GetParticleSpeedByIndex(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleSpeedByIndex not implmented.");
		return NULL; 
	}
	virtual Point3* GetParticleSpeedByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleSpeedByBornIndex not implmented.");
		return NULL; 
	}
	virtual void SetParticleSpeedByIndex(int index, Point3 speed) { ; }
	virtual void SetParticleSpeedByBornIndex(int id, Point3 speed) { ; }
	virtual Point3* GetParticleSpeed() { return NULL; }
	virtual void SetParticleSpeed(Point3 speed) { ; }

	// Implemented by the Plug-In.
	// The following six methods define orientation of the specified particle in the current state.
	// Particle is specified by either its index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		Point3 orient
	//			orientation of the particle. The orientation is defined by incremental rotations
	//			by world axes X, Y and Z. The rotation values are in degrees.
	virtual Point3* GetParticleOrientationByIndex(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleOrientationByIndex not implmented.");
		return NULL; 
	}
	virtual Point3* GetParticleOrientationByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleOrientationByBornIndex not implmented.");
		return NULL; 
	}
	virtual void SetParticleOrientationByIndex(int index, Point3 orient) { ; }
	virtual void SetParticleOrientationByBornIndex(int id, Point3 orient) { ; }
	virtual Point3* GetParticleOrientation() { return NULL; }
	virtual void SetParticleOrientation(Point3 orient) { ; }

	// Implemented by the Plug-In.
	// The following six methods define angular speed of the specified particle in the current state.
	// Particle is specified by either its index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		AngAxis spin
	//			angular speed of the particle in rotation per frame
	//			axis defines rotation axis, angle defines rotation amount per frame
	virtual AngAxis* GetParticleSpinByIndex(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleSpinByIndex not implmented.");
		return NULL; 
	}
	virtual AngAxis* GetParticleSpinByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleSpinByBornIndex not implmented.");
		return NULL; 
	}
	virtual void SetParticleSpinByIndex(int index, AngAxis spin) { ; }
	virtual void SetParticleSpinByBornIndex(int id, AngAxis spin) { ; }
	virtual AngAxis* GetParticleSpin() { return NULL; }
	virtual void SetParticleSpin(AngAxis spin) { ; }

	// Implemented by the Plug-In.
	// The following twelve methods define scale factor of the specified particle in the current state.
	// The XYZ form is used for non-uniform scaling
	// Particle is specified by either its index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		float scale
	//			uniform scale factor
	//		Point3 scale
	//			scale factor for each local axis of the particle
	virtual float GetParticleScaleByIndex(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleScaleByIndex not implmented.");
		return 1.0f; 
	}
	virtual float GetParticleScaleByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleScaleByBornIndex not implmented.");
		return 1.0f; 
	}
	virtual void SetParticleScaleByIndex(int index, float scale) { ; }
	virtual void SetParticleScaleByBornIndex(int id, float scale) { ; }
	virtual float GetParticleScale() { return 1.0f; }
	virtual void SetParticleScale(float scale) { ; }
	virtual Point3* GetParticleScaleXYZByIndex(int index) {
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleScaleXYZByIndex not implmented.");
		return NULL; 
	}
	virtual Point3* GetParticleScaleXYZByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleScaleXYZByBornIndex not implmented.");
		return NULL; 
	}
	virtual void SetParticleScaleXYZByIndex(int index, Point3 scale) { ; }
	virtual void SetParticleScaleXYZByBornIndex(int id, Point3 scale) { ; }
	virtual Point3* GetParticleScaleXYZ() { return NULL; }
	virtual void SetParticleScaleXYZ(Point3 scale) { ; }

	// Implemented by the Plug-In.
	// The following six methods define transformation matrix of the specified particle in the current state.
	// Particle is specified by either its index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		Matrix3 tm
	//			transformation matrix of the particle
	virtual Matrix3* GetParticleTMByIndex(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleTMByIndex not implmented.");
		return NULL; 
	}
	virtual Matrix3* GetParticleTMByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleTMByBornIndex not implmented.");
		return NULL; 
	}
	virtual void SetParticleTMByIndex(int index, Matrix3 tm) { ; }
	virtual void SetParticleTMByBornIndex(int id, Matrix3 tm) { ; }
	virtual Matrix3* GetParticleTM() { return NULL; }
	virtual void SetParticleTM(Matrix3 tm) { ; }

	// Implemented by the Plug-In.
	// The following six methods define selection status of the specified particle in the current state.
	// Particle is specified by either its index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		bool selected
	//			selection status of the particle
	virtual bool GetParticleSelectedByIndex(int index) { return true; }
	virtual bool GetParticleSelectedByBornIndex(int id) { return true; }
	virtual void SetParticleSelectedByIndex(int index, bool selected) { ; }
	virtual void SetParticleSelectedByBornIndex(int id, bool selected) { ; }
	virtual bool GetParticleSelected() { return true; }
	virtual void SetParticleSelected(bool selected) { ; }


	// Implemented by the Plug-In.
	// The following seven methods define shape of the specified particle in the current state.
	// Particle is specified by either its index in the particle group or particle system, or by its born index
	// if no index is specified then the "current" index is used
	// Parameters:
	//		int id
	//			particle born index
	//		int index
	//			particle index in the particle group
	//		Mesh* shape
	//			shape of the particle
	virtual Mesh* GetParticleShapeByIndex(int index) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleShapeByIndex not implmented.");
		return NULL; 
	}
	virtual Mesh* GetParticleShapeByBornIndex(int id) { 
		ESS_LOG_WARNING("IAlembicParticlesExt::GetParticleShapeByBornIndex not implmented.");
		return NULL; 
	}
	virtual void SetParticleShapeByIndex(int index, Mesh* shape) { ; }
	virtual void SetParticleShapeByBornIndex(int id, Mesh* shape) { ; }
	virtual Mesh* GetParticleShape() { return NULL; }
	virtual void SetParticleShape(Mesh* shape) { ; }
	// set the same shape for all particles
	virtual void SetGlobalParticleShape(Mesh* shape) { ; }
};