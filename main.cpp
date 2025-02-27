// for std
#include <iostream>

// for opencv
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <boost/concept_check.hpp>

// for g2o
#include <g2o/core/sparse_optimizer.h>
#include <g2o/core/block_solver.h>
#include <g2o/core/robust_kernel.h>
#include <g2o/core/robust_kernel_impl.h>
#include <g2o/core/optimization_algorithm_levenberg.h>
#include <g2o/solvers/cholmod/linear_solver_cholmod.h>
#include <g2o/types/slam3d/se3quat.h>
#include <g2o/types/sba/types_six_dof_expmap.h>




int findCorrespondingPoints( const cv::Mat& img1, const cv::Mat& img2,
        std::vector<cv::Point2f>& points1, std::vector<cv::Point2f>& points2);



// intrinsic camera parameters
double cx = 325.5;
double cy = 253.5;
double fx = 518.0;
double fy = 519.0;



int main( int argc, char** argv )
{
    if(argc != 3)
    {
        std::cout << "Usage: g2o_example img1, img2" << std::endl;
        exit(1);
    }

    cv::Mat img1 = cv::imread( argv[1] );
    cv::Mat img2 = cv::imread( argv[2] );

    std::vector<cv::Point2f> pts1, pts2;
    if( findCorrespondingPoints( img1, img2, pts1, pts2 ) == false )
    {
        std::cout << "no enough matching point!" << std::endl;
        return 0;
    }
    std::cout << "finding ! :" << pts1.size() << " pair matched points" << std::endl;

    g2o::SparseOptimizer optimizer;

    g2o::BlockSolver_6_3::LinearSolverType* linearSolver =  new
        g2o::LinearSolverCholmod<g2o::BlockSolver_6_3::PoseMatrixType> ();

    g2o::BlockSolver_6_3* block_solver = new g2o::BlockSolver_6_3( linearSolver );

    g2o::OptimizationAlgorithmLevenberg* algorithm = new
        g2o::OptimizationAlgorithmLevenberg( block_solver );

    optimizer.setAlgorithm( algorithm );
    optimizer.setVerbose( false );


    // adding node
    // 2 pose node
    for(int i = 0; i < 2; ++i)
    {
        g2o::VertexSE3Expmap* v = new g2o::VertexSE3Expmap();
        v->setId(i);
        if( i== 0 )
            v->setFixed( true );  // fix the first node
        // the predetermined value is identity pose
        v->setEstimate( g2o::SE3Quat() );
        optimizer.addVertex( v );
    }


    // set feature point, based on the img1
    for(size_t i = 0; i < pts1.size(); ++i)
    {
        g2o::VertexSBAPointXYZ* v = new g2o::VertexSBAPointXYZ();
        v->setId( 2 + i );
        // set depth to 1
        double z = 1;
        double x = ( pts1[i].x - cx ) * z / fx;
        double y = ( pts1[i].y - cy ) * z / fy;
        v->setMarginalized( true );
        v->setEstimate( Eigen::Vector3d(x, y, z) );
        optimizer.addVertex( v );
    }


    // set intrinsic camera parameter
    g2o::CameraParameters* camera = new g2o::CameraParameters( fx, Eigen::Vector2d(cx, cy), 0 );
    camera->setId(0);
    optimizer.addParameter( camera );


    // set edges
    // img1
    std::vector<g2o::EdgeProjectXYZ2UV*> edges;
    for(size_t i = 0; i < pts1.size(); ++i)
    {
        g2o::EdgeProjectXYZ2UV* edge = new g2o::EdgeProjectXYZ2UV();
        edge->setVertex( 0, dynamic_cast<g2o::VertexSBAPointXYZ*>
                (optimizer.vertex(i + 2)) );
        edge->setVertex( 1, dynamic_cast<g2o::VertexSE3Expmap*>
                (optimizer.vertex(0)) );
        edge->setMeasurement( Eigen::Vector2d(pts1[i].x, pts1[i].y ) );
        edge->setInformation( Eigen::Matrix2d::Identity() );
        edge->setParameterId(0, 0);

        // keneral function
        edge->setRobustKernel( new g2o::RobustKernelHuber() );
        optimizer.addEdge( edge );
        edges.push_back( edge );
    }


    // img2
    for(size_t i = 0; i < pts2.size(); ++i)
    {
        g2o::EdgeProjectXYZ2UV* edge = new g2o::EdgeProjectXYZ2UV();
        edge->setVertex( 0, dynamic_cast<g2o::VertexSBAPointXYZ*>
            (optimizer.vertex(i+2)) );
        edge->setVertex( 1, dynamic_cast<g2o::VertexSE3Expmap*>
                (optimizer.vertex(1)) );
        edge->setMeasurement( Eigen::Vector2d(pts2[i].x, pts2[i].y ) );
        edge->setInformation( Eigen::Matrix2d::Identity() );
        edge->setParameterId(0, 0);

        // keneral function
        edge->setRobustKernel( new g2o::RobustKernelHuber() );
        optimizer.addEdge( edge );
        edges.push_back( edge );
    }


    std::cout << "starting optimize" << std::endl;
    optimizer.setVerbose( true );
    optimizer.initializeOptimization();
    optimizer.optimize(10);
    std::cout << "finish optimize" << std::endl;

    g2o::VertexSE3Expmap* v = dynamic_cast<g2o::VertexSE3Expmap*>( optimizer.vertex(1) );

    Eigen::Isometry3d pose =v->estimate();
    std::cout << "Pose = " << std::endl << pose.matrix() << std::endl;

    for( size_t i = 0; i < pts1.size(); ++i )
    {
        g2o::VertexSBAPointXYZ* v = dynamic_cast<g2o::VertexSBAPointXYZ*>
            (optimizer.vertex( i + 2 ));
        std::cout << "vertex id " << i + 2 << ", pos = ";
        Eigen::Vector3d pos = v->estimate();
        std::cout << pos(0) << ", " << pos(1) << ", " << pos(2) << std::endl;
    }

    int inliers = 0;
    for( auto e : edges )
    {
        e->computeError();
        if( e->chi2() > 1 )
        {
            std::cout << "error = " << e->chi2() << std::endl;
        }
        else
        {
            inliers++;
        }
    }

    std::cout << "inliers in total points: " <<
        inliers << "/" << pts1.size() + pts2.size() << std::endl;
    optimizer.save("ba.g2o");
    return 0;
}




int findCorrespondingPoints( const cv::Mat& img1, const cv::Mat& img2,
        std::vector<cv::Point2f>& points1, std::vector<cv::Point2f>& points2 )
{
    cv::Ptr<cv::ORB> orb = cv::ORB::create();
    std::vector<cv::KeyPoint> kp1, kp2;
    cv::Mat desp1, desp2;
    orb->detectAndCompute(img1, cv::Mat(), kp1, desp1 );
    orb->detectAndCompute(img2, cv::Mat(), kp2, desp2 );

    std::cout << "finding " << kp1.size() << " and "
        << kp2.size() << "keypoints respectively" << std::endl;

    cv::Ptr<cv::DescriptorMatcher> matcher =
        cv::DescriptorMatcher::create( "BruteForce-Hamming" );

    double knn_match_ratio = 0.8;
    std::vector< std::vector<cv::DMatch> > matches_knn;
    matcher->knnMatch( desp1, desp2, matches_knn, 2 );
    std::vector< cv::DMatch > matches;
    for(size_t i = 0; i < matches_knn.size(); ++i)
    {
        if( matches_knn[i][0].distance < knn_match_ratio * matches_knn[i][1].distance )
            matches.push_back( matches_knn[i][0] );
    }

    if( matches.size() <= 20 ) // too few matched points
        return false;

    for(auto m : matches)
    {
        points1.push_back( kp1[m.queryIdx].pt );
        points2.push_back( kp2[m.trainIdx].pt );
    }

    return true;
}
