#include <slam/particle_filter.hpp>
#include <slam/occupancy_grid.hpp>
#include <common/angle_functions.hpp>
#include <lcmtypes/pose_xyt_t.hpp>
#include <cassert>


ParticleFilter::ParticleFilter(int numParticles)
: kNumParticles_ (numParticles)
{
    assert(kNumParticles_ > 1);
    posterior_.resize(kNumParticles_);
}


void ParticleFilter::initializeFilterAtPose(const pose_xyt_t& pose)
{
    ///////////// TODO: Implement your method for initializing the particles in the particle filter /////////////////
    double sampleWeight = 1/kNumParticles_;
    posteriorPose_ = pose;

    std::random_device rd;
    std::mt19937 generator(rd());
    std::normal_distribution<> dist(0.0, 0.01);

    for (auto& p : posterior_){
        p.pose.x = posteriorPose_.x + dist(generator);
        p.pose.y = posteriorPose_.y + dist(generator);
        p.pose.theta = wrap_to_pi(posteriorPose_.theta + dist(generator));
        p.pose.utime = pose.utime;
        p.parent_pose = p.pose;
        p.weight = sampleWeight;
    }

    posterior_.back().pose = pose;

}


pose_xyt_t ParticleFilter::updateFilter(const pose_xyt_t&      odometry,
                                        const lidar_t& laser,
                                        const OccupancyGrid&   map)
{
    // Only update the particles if motion was detected. If the robot didn't move, then
    // obviously don't do anything.
    bool hasRobotMoved = actionModel_.updateAction(odometry);

    if(hasRobotMoved)
    {
        auto prior = resamplePosteriorDistribution();
        auto proposal = computeProposalDistribution(prior);
        posterior_ = computeNormalizedPosterior(proposal, laser, map);
        posteriorPose_ = estimatePosteriorPose(posterior_);
    }

    posteriorPose_.utime = odometry.utime;

    return posteriorPose_;
    // return posterior_;

}

pose_xyt_t ParticleFilter::updateFilterActionOnly(const pose_xyt_t& odometry)
{
    // Only update the particles if motion was detected. If the robot didn't move, then
    // obviously don't do anything.
    bool hasRobotMoved = actionModel_.updateAction(odometry);

    if(hasRobotMoved)
    {
        // auto prior = resamplePosteriorDistribution();
        auto proposal = computeProposalDistribution(posterior_);
        posterior_ = proposal;
    }

    posteriorPose_ = odometry;

    return posteriorPose_;
}



pose_xyt_t ParticleFilter::poseEstimate(void) const
{
    return posteriorPose_;
}


particles_t ParticleFilter::particles(void) const
{
    particles_t particles;
    particles.num_particles = posterior_.size();
    particles.particles = posterior_;
    return particles;
}


std::vector<particle_t> ParticleFilter::resamplePosteriorDistribution(void)
{
    //////////// TODO: Implement your algorithm for resampling from the posterior distribution ///////////////////

    std::vector<particle_t> prior;

    int i = 0;
    double M_inv = 1.0 / kNumParticles_; //float
    double c,r;
    // r = rand() % M_inv;  //might not work (% of a float)

    r = (((double) rand()) / (double) RAND_MAX) * M_inv;
    // printf("r %f --- M_inv %f\n",r, M_inv);
    c = posterior_[0].weight;
    for (int m = 0; m < kNumParticles_; m++){  // start from m=0 and i=0
        double U = r + m * M_inv;
        while (U > c) {
            i++;
            c += posterior_[i].weight;
        }
        prior.push_back(posterior_[i]);
    }
    return prior;
}


std::vector<particle_t> ParticleFilter::computeProposalDistribution(const std::vector<particle_t>& prior)
{
    //////////// TODO: Implement your algorithm for creating the proposal distribution by sampling from the ActionModel
    std::vector<particle_t> proposal;

    for (auto& p : prior){
        proposal.push_back(actionModel_.applyAction(p));
    }

    return proposal;
}


std::vector<particle_t> ParticleFilter::computeNormalizedPosterior(const std::vector<particle_t>& proposal,
                                                                   const lidar_t& laser,
                                                                   const OccupancyGrid&   map)
{
    /////////// TODO: Implement your algorithm for computing the normalized posterior distribution using the
    ///////////       particles in the proposal distribution

    /// might fuck up cuz proposal is a const variable

    double wSum = 0.0;
    double tolerance = 0.000001;
    std::vector<particle_t> posterior;
    double w;

    for(auto& p : proposal){
        particle_t tempVar = p;
        w = sensorModel_.likelihood(p,laser,map);
        if(w < tolerance){
            w = tolerance;
        }
        tempVar.weight = w;
        posterior.push_back(tempVar);
        wSum += tempVar.weight;
    }

    // printf("last weight: %f ---- Weight sum: %f\n", w, wSum);

    for(auto& p : posterior){
        p.weight /= wSum;
        // printf("Likelihood: %f ----- ", p.weight);
        // printf("Particle x: %f\n", p.pose.x );
    }
    // printf("\nproposal x y %d %d -----", proposal.pose.x, proposal.pose.y);
    // printf("posterior x y %d %d", posterior.pose.x, posterior.pose.y);
    return posterior;
}


pose_xyt_t ParticleFilter::estimatePosteriorPose(const std::vector<particle_t>& posterior)
{
    //////// TODO: Implement your method for computing the final pose estimate based on the posterior distribution
    pose_xyt_t pose;
    double weightedSin = 0.0;
    double weightedCos = 0.0;

    for(auto& p : posterior){
        pose.x += p.weight * p.pose.x;
        pose.y += p.weight * p.pose.y;
        weightedSin += p.weight * std::sin(p.pose.theta);
        weightedCos += p.weight * std::cos(p.pose.theta);
    }
    pose.theta = std::atan2(weightedSin, weightedCos);
    // printf("\npose x y %f %f %f -----", pose.x, pose.y, pose.theta);
    return pose;
}
