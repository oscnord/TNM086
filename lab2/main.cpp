
#include <sgct.h>

#include <osgViewer/Viewer>
#include <osgDB/ReadFile>
#include <osg/MatrixTransform>

#include <osg/ComputeBoundsVisitor>
#include <osg/Material>

#include <osg/Version>
#include <osg/Node>
#include <osg/PositionAttitudeTransform>
#include <osg/AnimationPath>
#include <osgViewer/Viewer>
#include <osgUtil/Simplifier>
#include <osgUtil/Optimizer>
#include <osg/ShapeDrawable>
#include <osg/CopyOp>
#include <osgUtil/IntersectVisitor>

sgct::Engine * gEngine;

#define WAND_SENSOR_IDX 0
#define HEAD_SENSOR_IDX 1

// OSG stuff
osgViewer::Viewer * mViewer;
osg::ref_ptr<osg::Group> mRootNode;
osg::ref_ptr<osg::MatrixTransform> mSGCTTrans;
osg::ref_ptr<osg::MatrixTransform> mSceneTrans;
osg::ref_ptr<osg::FrameStamp> mFrameStamp; //to sync osg animations across cluster
osg::ref_ptr<osg::Geometry> linesGeom;

osg::ref_ptr<osgUtil::LineSegmentIntersector> wandLine;
osg::ref_ptr<osg::Node> intersectedNode = nullptr;

osg::Vec3d wand_start(0,-1,0);
osg::Vec3d wand_end(0,0,0);
glm::mat4 wand_matrix;
glm::mat4 head_matrix;
glm::vec3 wand_startPos;
glm::mat4 wand_startMat;

// callbacks
void myInitOGLFun();
void myPreSyncFun();
void myPostSyncPreDrawFun();
void myDrawFun();
void myEncodeFun();
void myDecodeFun();
void myCleanUpFun();
void keyCallback(int key, int action);

// other functions
void initOSG();
void createOSGScene();
void setupLightSource();
osg::Geode* createWand();
void IntersectionsCheck();

osg::ref_ptr<osg::Texture2D> addTexture();


osg::ref_ptr<osg::Node>            mModel;
osg::ref_ptr<osg::Node>            mNewModel;

bool isTouched = false;
bool isIntersected = false;
int scalene = 0;
bool isMoving = false;

//OSG support functions
osg::AnimationPath::ControlPoint createPoint(osg::Vec3 position, osg::Vec3 scale);
osg::ref_ptr<osg::Geode> createGround( int dimX, int dimY, float intervalX, float intervalY );
osg::ref_ptr<osg::HeightField> createHeightField( int dimX, int dimY, float intervalX, float intervalY);
osg::ref_ptr<osg::Texture2D> addTexture();
//cont.. 
void setHeights ( osg::ref_ptr<osg::HeightField> field, osg::ref_ptr<osg::Image> heightMap);
void addPathTo( osg::ref_ptr<osg::PositionAttitudeTransform> nodeTransform);
void addPoints( osg::ref_ptr<osg::AnimationPath> path );


//variables to share across cluster
sgct::SharedDouble curr_time(0.0);
sgct::SharedDouble dist(-2.0);
sgct::SharedVector<glm::mat4> sharedTransforms;
sgct::SharedString sharedText;
sgct::SharedVector<bool> theButtons;

sgct::SharedBool wireframe(false);
sgct::SharedBool info(false);
sgct::SharedBool stats(false);
sgct::SharedBool takeScreenshot(false);
sgct::SharedBool light(true);

// Simple initial navigation based on arrow buttons
bool arrowButtons[4];
enum directions { FORWARD = 0, BACKWARD, LEFT, RIGHT };
const double navigation_speed = 1.0;



int main( int argc, char* argv[] ) {
  gEngine = new sgct::Engine( argc, argv );

  gEngine->setInitOGLFunction( myInitOGLFun );
  gEngine->setPreSyncFunction( myPreSyncFun );
  gEngine->setPostSyncPreDrawFunction( myPostSyncPreDrawFun );
  gEngine->setDrawFunction( myDrawFun );
  gEngine->setCleanUpFunction( myCleanUpFun );
  gEngine->setKeyboardCallbackFunction( keyCallback );

  //fix incompability with warping and OSG
  sgct_core::ClusterManager::instance()->setMeshImplementation( sgct_core::ClusterManager::DISPLAY_LIST );

  for(int i=0; i<4; i++)
    arrowButtons[i] = false;

  if( !gEngine->init() ) {
    delete gEngine;
    return EXIT_FAILURE;
  }

  sgct::SharedData::instance()->setEncodeFunction( myEncodeFun );
  sgct::SharedData::instance()->setDecodeFunction( myDecodeFun );

  // Main loop
  gEngine->render();

  // Clean up
  delete gEngine;

  // Exit program
  exit( EXIT_SUCCESS );
}

void myInitOGLFun() {
  initOSG();
  createOSGScene();
  setupLightSource();

  //only store the tracking data on the master node
  if( !gEngine->isMaster() ) return;

  for(size_t i = 0; i < sgct::Engine::getTrackingManager()->getNumberOfTrackers(); i++){
    sgct::SGCTTracker * trackerPtr = sgct::Engine::getTrackingManager()->getTrackerPtr(i);

    for(size_t j=0; j<trackerPtr->getNumberOfDevices(); j++){
      sgct::SGCTTrackingDevice * devicePtr = trackerPtr->getDevicePtr(j);

      if (devicePtr->hasSensor())
        sharedTransforms.addVal( glm::mat4(1.0f) );
      
      if( devicePtr->hasButtons() ){
        for( int id = 0 ; id < devicePtr->getNumberOfButtons() ; ++id ) {
            theButtons.addVal(0); // id or 0 ?
        }
          
      }
    }
  }
  sharedText.setVal(" "); //< Space since SGCT hangs otherwise
}

/*
 * 
 * CREATE OSGScene 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */

void createOSGScene() {

  mRootNode->addChild(createWand() );

  
  osg::ref_ptr<osg::MatrixTransform> mModelTrans;
  osg::ref_ptr<osg::MatrixTransform> mNewModelTrans;

  mSGCTTrans         = new osg::MatrixTransform();
  mSceneTrans        = new osg::MatrixTransform();
  mModelTrans        = new osg::MatrixTransform();
  mNewModelTrans     = new osg::MatrixTransform();

  //rotate osg coordinate system to match sgct
  mModelTrans->preMult(osg::Matrix::rotate(glm::radians(-90.0f), 1.0f, 0.0f, 0.0f));
  mNewModelTrans->preMult(osg::Matrix::rotate(glm::radians(-90.0f), 1.0f, 0.0f, 0.0f));
  
  
  mRootNode->addChild( mSGCTTrans.get() );
  mSGCTTrans->addChild( mSceneTrans.get() );
  mSceneTrans->addChild( mModelTrans.get() );
  mSceneTrans->addChild( mNewModelTrans.get() );
  
  
  
  //loading our selected object
  sgct::MessageHandler::instance()->print("Loading model 'godtycklig.file'...\n");
  mNewModel = osgDB::readNodeFile("files/dumptruck.osg");

  if (!mNewModel.valid()) {
    sgct::MessageHandler::instance()->print("Failed to read model!\n");
    return;
  }

  sgct::MessageHandler::instance()->print("Model loaded successfully!\n");
  mNewModelTrans->addChild(mNewModel.get());
  
  
  
  //loading airplane object
  sgct::MessageHandler::instance()->print("Loading model 'airplane.ive'...\n");
  mModel = osgDB::readNodeFile("files/airplane.ive");

  if (!mModel.valid()) {
    sgct::MessageHandler::instance()->print("Failed to read model!\n");
    return;
  }

  sgct::MessageHandler::instance()->print("Model loaded successfully!\n");
  mModelTrans->addChild(mModel.get());

  //get the bounding box
  osg::ComputeBoundsVisitor cbv;
  osg::BoundingBox &bb(cbv.getBoundingBox());
  
  //add boundingboxes to model
  mModel->accept( cbv );
  mNewModel->accept( cbv );

  osg::Vec3f tmpVec;
  tmpVec = bb.center();
  osg::Vec3f planeTranslVec(0.0,0.0,50.0);
  //planeTranslVec = (0.0f,0.0f,10.0f);

  // translate model center to origin
  mModelTrans->postMult(osg::Matrix::translate( -tmpVec ) );
  mModelTrans->postMult(osg::Matrix::translate( planeTranslVec ) );
  
  mNewModelTrans->postMult(osg::Matrix::translate( -tmpVec ) );
  
  // scale model to a manageable size
  double scale = 0.1 / bb.radius();
  mModelTrans->postMult(osg::Matrix::scale(2*scale, 2*scale, 2*scale));
  mNewModelTrans->postMult(osg::Matrix::scale(scale, scale, scale));
  
  sgct::MessageHandler::instance()->print("Model bounding sphere center:\tx=%f\ty=%f\tz=%f\n", tmpVec[0], tmpVec[1], tmpVec[2] );
  sgct::MessageHandler::instance()->print("Model bounding sphere radius:\t%f\n", bb.radius() );

  //disable face culling
  mModel->getOrCreateStateSet()->setMode( GL_CULL_FACE,
                                          osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
}

void myPreSyncFun() {
  if (!gEngine->isMaster())
    return;

  curr_time.setVal( sgct::Engine::getTime() );

  if( arrowButtons[FORWARD] )
    dist.setVal( dist.getVal() + (navigation_speed * gEngine->getDt()));

  if( arrowButtons[BACKWARD] )
    dist.setVal( dist.getVal() - (navigation_speed * gEngine->getDt()));

  std::stringstream message;

  size_t index = 0;
  for(size_t i = 0; i < sgct::Engine::getTrackingManager()->getNumberOfTrackers(); i++){
    sgct::SGCTTracker * trackerPtr = sgct::Engine::getTrackingManager()->getTrackerPtr(i);

    for(size_t j = 0; j < trackerPtr->getNumberOfDevices(); j++){
      sgct::SGCTTrackingDevice * devicePtr = trackerPtr->getDevicePtr(j);

      message << "Device " << i << " on tracker " << j << std::endl;

      if( devicePtr->hasSensor() ){
        sharedTransforms.setValAt( index, devicePtr->getWorldTransform() );
        message << "Position:" << std::endl << "  "
                << devicePtr->getPosition().x << ", "
                << devicePtr->getPosition().y << ", "
                << devicePtr->getPosition().z << std::endl;
        message << "Euler angles:" << std::endl << "  "
                << devicePtr->getEulerAngles().x << ", "
                << devicePtr->getEulerAngles().y << ", "
                << devicePtr->getEulerAngles().z << std::endl;
        index++;
      }

      if( devicePtr->hasButtons() ){
        message << "Buttons:" << std::endl << "  ";
        for( int idx = 0 ; idx < devicePtr->getNumberOfButtons() ; ++idx ){
          message << devicePtr->getButton(idx) ? "1" : "0";
          std::cout << devicePtr->getButton(idx) ? "1" : "0" ;
          theButtons.setValAt(idx, devicePtr->getButton(idx));
        }
        message << std::endl;
      }

      if( devicePtr->hasAnalogs() ){
        message << "Analogs:" << std::endl << "  ";
        for( int idx = 0 ; idx < devicePtr->getNumberOfAxes() ; ++idx ){
          message << "  " << devicePtr->getAnalog(idx) << std::endl;
        }
      }
      message << std::endl;
    }
  }

  if (message.str().size() < 2)
    message << " "; //< Space postMultsince SGCT hangs otherwise
  sharedText.setVal(message.str());
}

void myPostSyncPreDrawFun() {
  gEngine->setWireframe(wireframe.getVal());
  gEngine->setDisplayInfoVisibility(info.getVal());
  gEngine->setStatsGraphVisibility(stats.getVal());
  
  bool point = false;
  bool crosshair = false;
  
  //Update position if button is pressed
  if(theButtons.getSize()) {
    if(theButtons.getValAt(1)){
       //point mode
       point = true;
       isMoving = true;
       //isTouched = true; //for testing
    }
    else if(theButtons.getValAt(0)){
       //crosshair mode
       crosshair = true;
       isMoving = true;
       
       
    }
    else if(theButtons.getValAt(2)) {
      //Selection of model
      isTouched = true;
        if (theButtons.getValAt(4)) {
            //scaling of model
            scalene = 1;
        }
        else if (theButtons.getValAt(5)) {
            //scaling of model
            scalene = -1;
        }
        else {
            scalene = 0;
      }
    }
    else {
        scalene = 0;
        isTouched = false;
        isMoving = false;
    }
  }

  if (takeScreenshot.getVal()) {
    gEngine->takeScreenshot();
    takeScreenshot.setVal(false);
  }

  if (light.getVal())
    mRootNode->getOrCreateStateSet()->setMode( GL_LIGHTING,
                                               osg::StateAttribute::ON |
                                               osg::StateAttribute::OVERRIDE);
  else
    mRootNode->getOrCreateStateSet()->setMode( GL_LIGHTING,
                                               osg::StateAttribute::OFF |
                                               osg::StateAttribute::OVERRIDE);

  // Simple initial navigation based on arrow buttons
  mSceneTrans->setMatrix(osg::Matrix::translate(0.0, 0.0, dist.getVal()));

  // SGCT internal transformation from configuration file
  mSGCTTrans->setMatrix(osg::Matrix(glm::value_ptr(gEngine->getModelMatrix())));

  //update the frame stamp in the viewer to sync all
  //time based events in osg
  mFrameStamp->setFrameNumber( gEngine->getCurrentFrameNumber() );
  mFrameStamp->setReferenceTime( curr_time.getVal() );
  mFrameStamp->setSimulationTime( curr_time.getVal() );
  mViewer->setFrameStamp( mFrameStamp.get() );
  mViewer->advance( curr_time.getVal() ); //update


  // Update wand in OSG
  if( sharedTransforms.getSize() > WAND_SENSOR_IDX ){   
    glm::mat4 wand_matrix = sharedTransforms.getValAt(WAND_SENSOR_IDX);

    glm::vec3 wand_position = glm::vec3(wand_matrix*glm::vec4(0,0,0,1));
    //glm::quat wand_orientation = glm::quat_cast(wand_matrix);
    glm::mat3 wand_orientation = glm::mat3(wand_matrix);

    glm::vec3 start = wand_position;
    glm::vec3 end = wand_position + wand_orientation * glm::vec3(0,0,-1);
    osg::Vec3d wand_start = osg::Vec3(start.x, start.y, start.z);
    osg::Vec3d wand_end = osg::Vec3(end.x, end.y, end.z);

    osg::Vec3Array* vertices = new osg::Vec3Array();
    vertices->push_back(wand_start);
    vertices->push_back(wand_end);
    linesGeom->setVertexArray(vertices);
    wandLine->setStart(wand_start);
    wandLine->setEnd(wand_end);
  }
  else{
    //Debug drawing for wand even if there is no VRPN server
    osg::Vec3Array* vertices = new osg::Vec3Array();
    vertices->push_back(wand_start);
    vertices->push_back(wand_end);
    linesGeom->setVertexArray(vertices);
    wandLine->setStart(wand_start);
    wandLine->setEnd(wand_end);
  }

  
  
  
  std::cout << "Testing sharedTransforms > HEAD_SENSOR_IDX" << std::endl;
  //movement - only if we have a head to move ;)
  if( sharedTransforms.getSize() > HEAD_SENSOR_IDX ) {
      
      std::cout << "sharedTransgorm > HEAD_SENSOR_IDX" << std::endl;
      
    if (!isMoving) {
        //store initial position of wand for deadzone calculation
        wand_startPos = glm::vec3( wand_matrix * glm::vec4(0, 0, 0, 1) ); 
    }

    wand_matrix = sharedTransforms.getValAt(WAND_SENSOR_IDX);
    
    glm::vec3 wand_position = glm::vec3( wand_matrix*glm::vec4(0,0,0,1) );
    glm::mat3 wand_orientation = glm::mat3(wand_matrix);

    head_matrix = sharedTransforms.getValAt(HEAD_SENSOR_IDX);
    glm::vec3 head_position = glm::vec3( head_matrix*glm::vec4(0,0,0,1) );
    
    //intial static speed factor
    float speedFactor = 0.1*gEngine->getDt();
    
    //user sets speed, deadzone is 10 cm from original position
    speedFactor = ( length(wand_startPos - wand_position) < 0.1 ? 0 : length(wand_startPos - wand_position)/50 );
    
    //if we pull the control towards us it should go backwards
    int direction = ( length(wand_startPos - head_position) > length(wand_position - head_position) ) ? -1 : 1;
                        speedFactor *= direction;
    //Move the world in the opposite direction for the movement effect 
    if(point) {
        glm::vec3 translation = ( glm::mat3(wand_matrix) * glm::vec3(0, 0, -1)*speedFactor );
        
        std::cout << "In point mode!" << std::endl;
        
        mSceneTrans->postMult(osg::Matrix::translate(
                -osg::Vec3(translation.x, translation.y, translation.z)));
    
        std::cout << "Translation done." << std::endl;
        
    }
    
    
    else if (crosshair) {
      glm::vec3 translation = normalize(head_position - wand_position)*speedFactor;
      
      std::cout << "In crosshair mode!" << std::endl;
      
      mSceneTrans->postMult(osg::Matrix::translate(
      osg::Vec3(translation.x, translation.y, translation.z)));
      
      std::cout << "Crosshair translation done." << std::endl;
      std::cout << "Translated - x: " << translation.x << " y: " 
      << translation.y << " z: " << translation.z <<   std::endl;
    }
  }
  if (!isTouched) {
       //Save wand matrix for manipulation
       wand_startMat = wand_matrix;
  }
  
  //traverse if there are any tasks to do
  if (!mViewer->done()) {
    mViewer->eventTraversal();
    //update travelsal needed for pagelod object like terrain data etc.
    mViewer->updateTraversal();
    IntersectionsCheck();
  }
}

void IntersectionsCheck() {
    
    
    //std::cout << "start of IntersectionsCheck" << std::endl;
    //check all nodes for intesection
    osgUtil::IntersectionVisitor visitor;
    visitor.setIntersector(wandLine);
    
    //std::cout << "Start" << std::endl;
    mRootNode->accept(visitor);
    //7std::cout << "accepted" << std::endl;
    
    if( !intersectedNode && wandLine->containsIntersections() ) {
        //get intersection, store it and do something with the object
        //std::cout << "Get intersectionpoint" << std::endl;    
        osgUtil::LineSegmentIntersector::Intersection intersectInfo = 
        wandLine->getFirstIntersection();
        //std::cout << "Found wandLine!" << std::endl;
        osg::NodePath nPath = intersectInfo.nodePath;
        
        isIntersected = true;
        for( osg::NodePath::iterator it = nPath.begin() ; it != nPath.end(); ++it ) {
            //std::cout << "intersection loop" << std::endl;
            if((*it) == mNewModel || (*it) == mModel) {
                intersectedNode = (*it);
            }
        }
        
        osg::ref_ptr<osg::Material> material = (osg::Material*)intersectedNode
        ->getOrCreateStateSet()->getAttribute(osg::StateAttribute::MATERIAL);
        
        if(!material) {
            material = new osg::Material();
        }
        material->setAmbient (osg::Material::FRONT_AND_BACK, osg::Vec4(1, 1, 0, 1.0));
        material->setDiffuse (osg::Material::FRONT_AND_BACK, osg::Vec4(1, 1, 0, 1.0));
        
        intersectedNode->getOrCreateStateSet()->setAttributeAndModes(material.get(), osg::StateAttribute::OVERRIDE);

    }
    //std::cout << "First check passed" << std::endl;
    else if ( isTouched && intersectedNode ) {
        //std::cout << "else if 1" << std::endl;
        //object is touched -> highlight it
        osg::ref_ptr<osg::Material> material = (osg::Material*) intersectedNode
        ->getOrCreateStateSet()->getAttribute(osg::StateAttribute::MATERIAL);
        material->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0, 1, 0, 1.0));
        material->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(0, 1, 0, 1.0));
        intersectedNode->getOrCreateStateSet()->setAttributeAndModes(material.get(), osg::StateAttribute::OVERRIDE);
    
        //difference between starting wand orientation and current pos to determine the transformation
        glm::mat4 diff = wand_startMat;
        glm::mat4 diffInv = inverse(wand_matrix);

        osg::ref_ptr<osg::MatrixTransform> parent = intersectedNode->getParent(0)->asTransform()->asMatrixTransform();
        
        if(scalene != 0) {
            float scaleVal =0.05f;
            float scale = 1- (scaleVal*scalene);
        
            parent->postMult(osg::Matrix::scale(scale,scale,scale));
        }
        else {    
            parent->postMult(osg::Matrix(glm::value_ptr(inverse(diff*diffInv))));
        }
        wand_startMat = wand_matrix;
    }
    //std::cout << "Second check passed" << std::endl;
    else if(!wandLine->containsIntersections()) {
        //std::cout << "Else if 2" << std::endl;
        mModel->getOrCreateStateSet()->removeAttribute(osg::StateAttribute::MATERIAL);
        mNewModel->getOrCreateStateSet()->removeAttribute(osg::StateAttribute::MATERIAL);
        intersectedNode = NULL;
    } 
    //std::cout << "End" << std::endl;
        wandLine->reset();
       
    
}



void myDrawFun() {
  glLineWidth(2.0f);

  const int * curr_vp = gEngine->getCurrentViewportPixelCoords();
  mViewer->getCamera()->setViewport(curr_vp[0], curr_vp[1], curr_vp[2], curr_vp[3]);
  mViewer->getCamera()->setProjectionMatrix( osg::Matrix( glm::value_ptr(gEngine->getCurrentViewProjectionMatrix() ) ));

  mViewer->renderingTraversals();

	// draw text with OpenGL
	float textVerticalPos = static_cast<float>(gEngine->getCurrentWindowPtr()->getYResolution()) - 100.0f;
	int fontSize = 12;

	glColor3f(1.0f, 1.0f, 1.0f);
	sgct_text::print(sgct_text::FontManager::instance()->getFont( "SGCTFont", fontSize ),
		sgct_text::TextAlignMode::TOP_LEFT,
		120.0f, textVerticalPos,
		sharedText.getVal().c_str() );
}

void myEncodeFun() {
  sgct::SharedData::instance()->writeDouble( &curr_time );
  sgct::SharedData::instance()->writeVector( &sharedTransforms );
	sgct::SharedData::instance()->writeString( &sharedText );
  sgct::SharedData::instance()->writeBool( &wireframe );
  sgct::SharedData::instance()->writeBool( &info );
  sgct::SharedData::instance()->writeBool( &stats );
  sgct::SharedData::instance()->writeBool( &takeScreenshot );
  sgct::SharedData::instance()->writeBool( &light );
}

void myDecodeFun() {
  sgct::SharedData::instance()->readDouble( &curr_time );
  sgct::SharedData::instance()->readVector( &sharedTransforms );
  sgct::SharedData::instance()->readString( &sharedText );
  sgct::SharedData::instance()->readBool( &wireframe );
  sgct::SharedData::instance()->readBool( &info );
  sgct::SharedData::instance()->readBool( &stats );
  sgct::SharedData::instance()->readBool( &takeScreenshot );
  sgct::SharedData::instance()->readBool( &light );
}

void myCleanUpFun() {
  sgct::MessageHandler::instance()->print("Cleaning up osg data...\n");
  delete mViewer;
  mViewer = NULL;
}

void keyCallback(int key, int action) {
  if(!gEngine->isMaster())
    return;

  switch (key) {
  case 'S':
    if(action == SGCT_PRESS)
      stats.toggle();
    break;

  case 'I':
    if(action == SGCT_PRESS)
      info.toggle();
    break;

  case 'L':
    if(action == SGCT_PRESS)
      light.toggle();
    break;

  case 'W':
    if(action == SGCT_PRESS)
      wireframe.toggle();
    break;

  case 'Q':
    if(action == SGCT_PRESS)
      gEngine->terminate();
    break;

  case 'P':
  case SGCT_KEY_F10:
    if(action == SGCT_PRESS)
      takeScreenshot.setVal( true );
    break;

  case SGCT_KEY_UP:
    arrowButtons[FORWARD] = ((action == SGCT_REPEAT || action == SGCT_PRESS) ? true : false);
    break;

  case SGCT_KEY_DOWN:
    arrowButtons[BACKWARD] = ((action == SGCT_REPEAT || action == SGCT_PRESS) ? true : false);
    break;
    
  case SGCT_KEY_Y:
      isTouched = true;
      break;
  case SGCT_KEY_U:
      isTouched = false;
      break;
  }
}

void initOSG() {
  mRootNode = new osg::Group();
  osg::Referenced::setThreadSafeReferenceCounting(true);

  // Create the osgViewer instance
  mViewer = new osgViewer::Viewer;

  // Create a time stamp instance
  mFrameStamp    = new osg::FrameStamp();

  //run single threaded when embedded
  mViewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);

  // Set up osgViewer::GraphicsWindowEmbedded for this context
  osg::GraphicsContext::Traits *traits =
    new osg::GraphicsContext::Traits;

  osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> graphicsWindow =
    new osgViewer::GraphicsWindowEmbedded(traits);

  mViewer->getCamera()->setGraphicsContext(graphicsWindow.get());
  
  
  wandLine = new osgUtil::LineSegmentIntersector(wand_start, wand_end);
  
  

  //SGCT will handle the near and far planes
  mViewer->getCamera()->setComputeNearFarMode(osgUtil::CullVisitor::DO_NOT_COMPUTE_NEAR_FAR);
  mViewer->getCamera()->setClearColor( osg::Vec4( 0.0f, 0.0f, 0.0f, 0.0f) );

  //disable osg from clearing the buffers that will be done by SGCT
  GLbitfield tmpMask = mViewer->getCamera()->getClearMask();
  mViewer->getCamera()->setClearMask(tmpMask & (~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)));
    

  mViewer->setSceneData(mRootNode.get());
}

void setupLightSource() {
  osg::Light * light0 = new osg::Light;
  osg::Light * light1 = new osg::Light;
  osg::LightSource* lightSource0 = new osg::LightSource;
  osg::LightSource* lightSource1 = new osg::LightSource;

  light0->setLightNum( 0 );
  light0->setPosition( osg::Vec4( 5.0f, 5.0f, 10.0f, 1.0f ) );
  light0->setAmbient( osg::Vec4( 0.3f, 0.3f, 0.3f, 1.0f ) );
  light0->setDiffuse( osg::Vec4( 0.8f, 0.8f, 0.8f, 1.0f ) );
  light0->setSpecular( osg::Vec4( 0.1f, 0.1f, 0.1f, 1.0f ) );
  light0->setConstantAttenuation( 1.0f );

  lightSource0->setLight( light0 );
  lightSource0->setLocalStateSetModes( osg::StateAttribute::ON );
  lightSource0->setStateSetModes( *(mRootNode->getOrCreateStateSet()), osg::StateAttribute::ON );

  light1->setLightNum( 1 );
  light1->setPosition( osg::Vec4( -5.0f, -2.0f, 10.0f, 1.0f ) );
  light1->setAmbient( osg::Vec4( 0.2f, 0.2f, 0.2f, 1.0f ) );
  light1->setDiffuse( osg::Vec4( 0.5f, 0.5f, 0.5f, 1.0f ) );
  light1->setSpecular( osg::Vec4( 0.2f, 0.2f, 0.2f, 1.0f ) );
  light1->setConstantAttenuation( 1.0f );

  lightSource1->setLight( light1 );
  lightSource1->setLocalStateSetModes( osg::StateAttribute::ON );
  lightSource1->setStateSetModes( *(mRootNode->getOrCreateStateSet()), osg::StateAttribute::ON );

  mRootNode->addChild( lightSource0 );
  mRootNode->addChild( lightSource1 );
}

osg::Geode* createWand() {

  osg::Geode* geode = new osg::Geode();

  linesGeom = new osg::Geometry();

  osg::Vec3Array* vertices = new osg::Vec3Array();
  vertices->push_back(osg::Vec3(0, 0, 0));
  vertices->push_back(osg::Vec3(1, 0, 0));
  linesGeom->setVertexArray(vertices);

  osg::Vec4Array* colors = new osg::Vec4Array;
  colors->push_back(osg::Vec4(0.3f,0.7f,0.4f,1.0f));
  linesGeom->setColorArray(colors, osg::Array::BIND_OVERALL);

  osg::Vec3Array* normals = new osg::Vec3Array;
  normals->push_back(osg::Vec3(0.0f,-1.0f,0.0f));
  linesGeom->setNormalArray(normals, osg::Array::BIND_OVERALL);

  linesGeom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES,0,2));
  geode->addDrawable(linesGeom);

  return geode;
}
