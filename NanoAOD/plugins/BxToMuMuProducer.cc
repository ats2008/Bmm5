#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/interface/EDProducer.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/ESHandle.h"

#include "DataFormats/PatCandidates/interface/Muon.h"
#include "DataFormats/PatCandidates/interface/PackedCandidate.h"
#include "DataFormats/PatCandidates/interface/CompositeCandidate.h"
#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "DataFormats/VertexReco/interface/Vertex.h"

#include "TrackingTools/Records/interface/TransientTrackRecord.h"
#include "TrackingTools/TransientTrack/interface/TransientTrack.h"
#include "TrackingTools/TransientTrack/interface/TransientTrackBuilder.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoVertex/KinematicFitPrimitives/interface/TransientTrackKinematicParticle.h"
#include "RecoVertex/KinematicFitPrimitives/interface/KinematicParticleFactoryFromTransientTrack.h"
#include "RecoVertex/KinematicFit/interface/KinematicParticleVertexFitter.h"
#include "RecoVertex/KinematicFit/interface/KinematicParticleFitter.h"
#include "RecoVertex/KinematicFit/interface/MassKinematicConstraint.h"
#include "RecoVertex/KinematicFit/interface/PointingKinematicConstraint.h"
#include "RecoVertex/KinematicFit/interface/KinematicConstrainedVertexFitter.h"
#include "RecoVertex/KalmanVertexFit/interface/KalmanVertexFitter.h"
#include "TrackingTools/PatternTools/interface/TwoTrackMinimumDistance.h"
#include "TrackingTools/IPTools/interface/IPTools.h"
#include "TrackingTools/GeomPropagators/interface/AnalyticalImpactPointExtrapolator.h"
#include "TMVA/Reader.h"

#include "CommonTools/CandUtils/interface/AddFourMomenta.h"

#include "CommonTools/Statistics/interface/ChiSquaredProbability.h"
#include "DataFormats/Math/interface/Vector3D.h"
#include <TLorentzVector.h>
#include <TVector.h>
#include <TMatrix.h>
#include <algorithm>

// 
// BxToMuMuProducer is designed for Bs/d->mumu analysis
//

typedef reco::Candidate::LorentzVector LorentzVector;

namespace {
  const float MuonMass_    = 0.10565837;
  const float MuonMassErr_ = 3.5*1e-9;
  const float KaonMass_    = 0.493677;
  const float KaonMassErr_ = 1.6e-5;
  const float pionMass_    = 0.139570;
  const float pionMassErr_ = 3.5e-7;
  const float JPsiMass_    = 3.0969;
  const float JPsiMassErr_ = 92.9e-6;
};


struct KinematicFitResult{
  bool treeIsValid;
  bool vertexIsValid;
  RefCountedKinematicVertex      refitVertex;
  RefCountedKinematicParticle    refitMother;
  RefCountedKinematicTree        refitTree;
  std::vector<RefCountedKinematicParticle> refitDaughters;
  float lxy, lxyErr, sigLxy, cosAlpha;
  KinematicFitResult():treeIsValid(false),vertexIsValid(false),
		       lxy(-1.0), sigLxy(-1.0), cosAlpha(-999.)
  {}

  bool valid() const {
    return treeIsValid and vertexIsValid;
  }

  void postprocess(const reco::BeamSpot& beamSpot)
  {
    if ( not valid() ) return;
    // displacement information
    TVector v(2);
    v[0] = refitVertex->position().x()-beamSpot.position().x();
    v[1] = refitVertex->position().y()-beamSpot.position().y();

    TMatrix errVtx(2,2);
    errVtx(0,0) = refitVertex->error().cxx();
    errVtx(0,1) = refitVertex->error().matrix()(0,1);
    errVtx(1,0) = errVtx(0,1);
    errVtx(1,1) = refitVertex->error().cyy();

    TMatrix errBS(2,2);
    errBS(0,0) = beamSpot.covariance()(0,0);
    errBS(0,1) = beamSpot.covariance()(0,1);
    errBS(1,0) = beamSpot.covariance()(1,0);
    errBS(1,1) = beamSpot.covariance()(1,1);
    
    lxy = sqrt(v.Norm2Sqr());
    lxyErr = sqrt( v*(errVtx*v) + v*(errBS*v) ) / lxy;
    if (lxyErr > 0) sigLxy = lxy/lxyErr;
    
    // compute cosAlpha
    v[0] = refitVertex->position().x()-beamSpot.position().x();
    v[1] = refitVertex->position().y()-beamSpot.position().y();
    TVector w(2);
    w[0] = refitMother->currentState().globalMomentum().x();
    w[1] = refitMother->currentState().globalMomentum().y();
    cosAlpha = v*w/sqrt(v.Norm2Sqr()*w.Norm2Sqr());
  }
  
  float mass() const
  {
    if ( not valid() ) return -1.0;
    return refitMother->currentState().mass();
  }

  float refit_mass(unsigned int i, unsigned int j) const
  {
    if ( not valid() ) return -1.0;
    if (i >= refitDaughters.size()) return -2.0;
    if (j >= refitDaughters.size()) return -3.0;
    if (refitDaughters.at(i)->currentState().globalMomentum().mag2()<0) return -4.0;
    if (refitDaughters.at(j)->currentState().globalMomentum().mag2()<0) return -5.0;
    auto momentum = refitDaughters.at(i)->currentState().globalMomentum() + 
      refitDaughters.at(j)->currentState().globalMomentum();
    auto energy1 = sqrt(refitDaughters.at(i)->currentState().globalMomentum().mag2() + 
			pow(refitDaughters.at(i)->currentState().mass(),2));
    auto energy2 = sqrt(refitDaughters.at(j)->currentState().globalMomentum().mag2() + 
			pow(refitDaughters.at(j)->currentState().mass(),2));
    return sqrt(pow(energy1+energy2,2)-momentum.mag2());
  }

  GlobalVector p3() const
  {
    if ( not valid() ) return GlobalVector();
    return refitMother->currentState().globalMomentum();
  }

  GlobalVector dau_p3(unsigned int i) const
  {
    if ( not valid() or i>=refitDaughters.size() ) return GlobalVector();
    return refitDaughters.at(i)->currentState().globalMomentum();
  }

  float massErr() const
  {
    if ( not valid() ) return -1.0;
    return sqrt(refitMother->currentState().kinematicParametersError().matrix()(6,6));
  }

  float chi2() const
  {
    if ( not valid() ) return -1.0;
    return refitVertex->chiSquared();
  }

  float ndof() const
  {
    return refitVertex->degreesOfFreedom();
  }

  float vtxProb() const
  {
    if ( not valid() ) return -1.0;
    return TMath::Prob((double)refitVertex->chiSquared(), int(rint(refitVertex->degreesOfFreedom())));
  }
  
};

struct KalmanVertexFitResult{
  float vtxProb;
  bool  valid;
  std::vector<LorentzVector> refitVectors;
  GlobalPoint position;
  GlobalError err;
  float lxy, lxyErr, sigLxy;

  KalmanVertexFitResult():vtxProb(-1.0),valid(false),lxy(-1.0),lxyErr(-1.0),sigLxy(-1.0){}

  float mass() const
  {
    if (not valid) return -1.0;
    LorentzVector p4;
    for (auto v: refitVectors)
      p4 += v;
    return p4.mass();
  }
  
  void postprocess(const reco::BeamSpot& bs)
  {
    if (not valid) return;
    // position of the beam spot at a given z value (it takes into account the dxdz and dydz slopes)
    reco::BeamSpot::Point bs_at_z(bs.position(position.z()));
    GlobalPoint xy_displacement(position.x() - bs_at_z.x(),
				position.y() - bs_at_z.y(),
				0);
    lxy = xy_displacement.perp();
    lxyErr = sqrt(err.rerr(xy_displacement));
    if (lxyErr > 0) sigLxy = lxy/lxyErr;
  }
};

struct DisplacementInformationIn3D{
  double decayLength, decayLengthErr, decayLength2, decayLength2Err, 
    distaceOfClosestApproach, distaceOfClosestApproachErr, 
    distaceOfClosestApproach2, distaceOfClosestApproach2Err, 
    longitudinalImpactParameter, longitudinalImpactParameterErr,
    longitudinalImpactParameter2, longitudinalImpactParameter2Err,
    cosAlpha;
  const reco::Vertex *pv,*pv2;
  int pvIndex,pv2Index;
  DisplacementInformationIn3D():decayLength(-1.0),decayLengthErr(0.),decayLength2(-1.0),decayLength2Err(0.),
				distaceOfClosestApproach(-1.0),distaceOfClosestApproachErr(0.0),
				distaceOfClosestApproach2(-1.0),distaceOfClosestApproach2Err(0.0),
				longitudinalImpactParameter2(0.0), longitudinalImpactParameter2Err(0.), 
				cosAlpha(-999.),
				pv(0),pv2(0),
				pvIndex(-1),pv2Index(-1)
  {};
};

LorentzVector makeLorentzVectorFromPxPyPzM(double px, double py, double pz, double m){
  double p2 = px*px+py*py+pz*pz;
  return LorentzVector(px,py,pz,sqrt(p2+m*m));
}

struct GenMatchInfo{
  int mu1_pdgId, mu1_motherPdgId, mu2_pdgId, mu2_motherPdgId, kaon1_pdgId, kaon1_motherPdgId,
    kaon2_pdgId, kaon2_motherPdgId, mm_pdgId, mm_motherPdgId, kmm_pdgId, kkmm_pdgId;
  float mu1_pt, mu2_pt, kaon1_pt, kaon2_pt, mm_mass, mm_pt, kmm_mass, kkmm_mass, kmm_pt, kkmm_pt;
  math::XYZPoint mm_prod_vtx, mm_vtx, kmm_prod_vtx, kkmm_prod_vtx;
  GenMatchInfo():mu1_pdgId(0), mu1_motherPdgId(0), mu2_pdgId(0), mu2_motherPdgId(0), 
		 kaon1_pdgId(0), kaon1_motherPdgId(0), kaon2_pdgId(0), kaon2_motherPdgId(0),
		 mm_pdgId(0), mm_motherPdgId(0), 
		 kmm_pdgId(0), kkmm_pdgId(0), mu1_pt(0), mu2_pt(0), 
		 kaon1_pt(0), kaon2_pt(0), mm_mass(0), mm_pt(0), 
		 kmm_mass(0), kkmm_mass(0), kmm_pt(0), kkmm_pt(0){}
};

struct GenEventInfo{};

struct CloseTrack{
  float svDoca, svDocaErr, svProb,
    pvDoca, pvDocaErr,
    impactParameterSignificanceBS;
  const pat::PackedCandidate* pfCand;
  CloseTrack(): svDoca(-1), svDocaErr(-1), svProb(-1),
		pvDoca(-1), pvDocaErr(-1),
		impactParameterSignificanceBS(-1),
		pfCand(0)
  {};
};


struct CloseTrackInfo{
  std::vector<CloseTrack> tracks;
  unsigned int nTracksByVertexProbability(double minProb = 0.1, 
					  double minIpSignificance = -1,
					  int pvIndex = -1,
					  const pat::PackedCandidate* ignoreTrack1 = 0)
  {
    unsigned int n = 0;
    for (auto track: tracks){
      if (ignoreTrack1 and track.pfCand==ignoreTrack1) continue;
      if (minIpSignificance>0 and track.impactParameterSignificanceBS<minIpSignificance) continue;
      if (track.svProb<minProb) continue;
      if (pvIndex >= 0 and int(track.pfCand->vertexRef().key())!=pvIndex) continue;
      n++;
    }
    return n;
  }
  unsigned int nTracksByDisplacementSignificance(double max_svDoca = 0.03, 
						 double maxSignificance = -1,
						 int pvIndex = -1,
						 const pat::PackedCandidate* ignoreTrack1 = 0)
  {
    unsigned int n = 0;
    for (auto track: tracks){
      if (track.svDoca>max_svDoca) continue;
      if (ignoreTrack1 and track.pfCand==ignoreTrack1) continue;
      if (maxSignificance>0 and (track.svDocaErr<=0 or 
				 track.svDoca/track.svDocaErr > maxSignificance) ) continue;
      if (pvIndex >= 0 and int(track.pfCand->vertexRef().key())!=pvIndex) continue;
      n++;
    }
    return n;
  }
  unsigned int nTracksByBetterMatch(double max_svDoca = 0.03, 
				    double maxSignificance = 2,
				    int pvIndex = -1,
				    const pat::PackedCandidate* ignoreTrack1 = 0)
  {
    unsigned int n = 0;
    for (auto track: tracks){
      if (track.svDoca>max_svDoca) continue;
      if (ignoreTrack1 and track.pfCand==ignoreTrack1) continue;
      if (maxSignificance>0 and (track.svDocaErr<=0 or 
				 track.svDoca/track.svDocaErr > maxSignificance) ) continue;
      if (track.svDocaErr<=0 or (track.pvDocaErr>0 and track.svDoca/track.svDocaErr > track.pvDoca/track.pvDocaErr) ) continue;
      if (pvIndex >= 0 and int(track.pfCand->vertexRef().key())!=pvIndex) continue;
      n++;
    }
    return n;
  }
  float minDoca(double max_svDoca = 0.03, 
		int pvIndex = -1,
		const pat::PackedCandidate* ignoreTrack1 = 0)
  {
    float doca = 99.;
    for (auto track: tracks){
      if (track.svDoca>max_svDoca) continue;
      if (ignoreTrack1 and track.pfCand==ignoreTrack1) continue;
      if (pvIndex >= 0 and int(track.pfCand->vertexRef().key())!=pvIndex) continue;
      if (doca>track.svDoca) doca = track.svDoca;
    }
    return doca;
  }

  void fillCandInfo(pat::CompositeCandidate& cand, int pvIndex, std::string name)
  {
    if (name!="") name += "_";
    cand.addUserInt(   name + "nTrks",       nTracksByVertexProbability(0.1,-1.0,pvIndex) );
    cand.addUserInt(   name + "nBMTrks",     nTracksByBetterMatch() );
    cand.addUserInt(   name + "nDisTrks",    nTracksByVertexProbability(0.1, 2.0,pvIndex) );
    cand.addUserInt(   name + "closetrk",    nTracksByDisplacementSignificance(0.03, -1, pvIndex) );
    cand.addUserInt(   name + "closetrks1",  nTracksByDisplacementSignificance(0.03, 1, pvIndex) );
    cand.addUserInt(   name + "closetrks2",  nTracksByDisplacementSignificance(0.03, 2, pvIndex) );
    cand.addUserInt(   name + "closetrks3",  nTracksByDisplacementSignificance(0.03, 3, pvIndex) );
    cand.addUserFloat( name + "docatrk",     minDoca(0.03, pvIndex) );
  }
};

struct BdtReaderData {
  float fls3d, alpha, pvips, iso, chi2dof, docatrk, closetrk, m1iso, m2iso, eta, m;
};


using namespace std;

///////////////////////////////////////////////////////////////////////////
///                             P L U G I N
///////////////////////////////////////////////////////////////////////////

class BxToMuMuProducer : public edm::EDProducer {
    
public:
    
  explicit BxToMuMuProducer(const edm::ParameterSet &iConfig);
    
  ~BxToMuMuProducer() override {};
    
    
private:
    
  virtual void produce(edm::Event&, const edm::EventSetup&);

  bool isGoodMuon(const pat::Muon& muon);
    
  KalmanVertexFitResult 
  vertexWithKalmanFitter( std::vector<const reco::Track*> trks, 
			 std::vector<float> masses);

  KalmanVertexFitResult 
  vertexMuonsWithKalmanFitter(const pat::Muon& muon1,
			      const pat::Muon& muon2);

  KinematicFitResult 
  vertexWithKinematicFitter(std::vector<const reco::Track*> trks,
			    std::vector<float> masses);

  KinematicFitResult 
  vertexMuonsWithKinematicFitter(const pat::Muon& muon1,
				 const pat::Muon& muon2);

  KinematicFitResult 
  vertexWithKinematicFitter(const pat::Muon& muon1,
			    const pat::Muon& muon2,
			    const pat::PackedCandidate& pfCand);

  /// BToKJPsiMuMuFitResult
  KinematicFitResult
  fitBToKJPsiMuMu( RefCountedKinematicParticle jpsi,
		   const pat::PackedCandidate& kaon,
		   bool applyJpsiMassConstraint);

  KinematicFitResult
  fitBToKJPsiMuMuNew( RefCountedKinematicTree jpsi,
		      const pat::PackedCandidate& kaon,
		      bool applyJpsiMassConstraint);
  KinematicFitResult
  fitBToKKMuMu( RefCountedKinematicTree jpsi,
		const pat::PackedCandidate& kaon1,
		const pat::PackedCandidate& kaon2,
		bool applyJpsiMassConstraint);

  KinematicFitResult
  refitWithPointingConstraint( RefCountedKinematicTree tree,
			       const reco::Vertex& primaryVertex);

  pair<double,double> computeDCA(const pat::PackedCandidate &kaon,
   				 reco::BeamSpot beamSpot);
  GenMatchInfo getGenMatchInfo( const edm::View<reco::GenParticle>& genParticles,
				const pat::Muon& muon1,
				const pat::Muon& muon2,
				const pat::PackedCandidate* kaon1 = 0,
				const pat::PackedCandidate* kaon2 = 0 );
  // Two track DOCA
  float 
  distanceOfClosestApproach( const reco::Track* track1,
			     const reco::Track* track2 );
  // Track to vertex DOCA
  Measurement1D
  distanceOfClosestApproach( const reco::Track* track,
			     RefCountedKinematicVertex vertex);
  Measurement1D 
  distanceOfClosestApproach( const reco::Track* track,
			     const reco::Vertex& vertex);

  DisplacementInformationIn3D 
  compute3dDisplacement(const KinematicFitResult& fit,
			const reco::VertexCollection& vertices,
			bool closestIn3D = true);

  CloseTrackInfo 
  findTracksCompatibleWithTheVertex(const pat::Muon& muon1,
				    const pat::Muon& muon2,
				    const KinematicFitResult& fit,
				    double maxDoca=0.03,
				    std::vector<const pat::PackedCandidate*> ignoreTracks = 
				    std::vector<const pat::PackedCandidate*>());
  float
  computeTrkMuonIsolation(const pat::Muon& muon, 
			  const pat::Muon& the_other_muon,
			  unsigned int primaryVertexIndex,
			  float minPt=0.5, float dR=0.5,
			  std::vector<const pat::PackedCandidate*> ignoreTracks = 
			  std::vector<const pat::PackedCandidate*>());
  float
  computeTrkMuMuIsolation(const pat::Muon& muon1, 
			  const pat::Muon& muon2,
			  unsigned int primaryVertexIndex,
			  float minPt=0.9, float dR=0.7,
			  std::vector<const pat::PackedCandidate*> ignoreTracks = 
			  std::vector<const pat::PackedCandidate*>());

  float
  otherVertexMaxProb(const pat::Muon& muon1, 
		     const pat::Muon& muon2,
		     float min_pt = 0.5,
		     float max_doca = 0.1,
		     std::vector<const pat::PackedCandidate*> ignoreTracks = 
		     std::vector<const pat::PackedCandidate*>());

  void 
  fillBtoJpsiKInfo(pat::CompositeCandidate& bCand,
		   const edm::Event& iEvent,
		   const KinematicFitResult& kinematicMuMuVertexFit,
		   const pat::Muon& muon1,
		   const pat::Muon& muon2,
		   const pat::PackedCandidate & kaon); 
  void
  fillBstoJpsiKKInfo(pat::CompositeCandidate& bCand,
		     const edm::Event& iEvent,
		     const KinematicFitResult& kinematicMuMuVertexFit,
		     const pat::Muon& muon1,
		     const pat::Muon& muon2,
		     const pat::PackedCandidate & kaon1,
		     const pat::PackedCandidate & kaon2
		    ); 
  
  void 
  fillBDTForBtoJpsiKThatEmulatesBmm(pat::CompositeCandidate& btokmmCand,
				    const pat::CompositeCandidate& dimuonCand,
				    const edm::Event& iEvent,
				    const KinematicFitResult& kinematicMuMuVertexFit,
				    const pat::Muon& muon1,
				    const pat::Muon& muon2,
				    const pat::PackedCandidate & kaon);
 
  void 
  fillMuMuInfo(pat::CompositeCandidate& dimuonCand,
	       const edm::Event& iEvent,
	       const KinematicFitResult& kinematicMuMuVertexFit,
	       const pat::Muon& muon1,
	       const pat::Muon& muon2); 

  float  computeAnalysisBDT(unsigned int event_idx);
  
  void setupTmvaReader(TMVA::Reader& reader, std::string file);


  // ----------member data ---------------------------
    
  edm::EDGetTokenT<reco::BeamSpot> beamSpotToken_;
  const reco::BeamSpot* beamSpot_;

  edm::EDGetTokenT<reco::VertexCollection> vertexToken_;
  edm::EDGetTokenT<std::vector<pat::Muon>> muonToken_;
  edm::EDGetTokenT<edm::View<pat::PackedCandidate>> pfCandToken_;
  edm::EDGetTokenT<edm::View<reco::GenParticle> >   prunedGenToken_;
  const edm::View<reco::GenParticle>* prunedGenParticles_;

  edm::ESHandle<TransientTrackBuilder> theTTBuilder_;
  edm::ESHandle<MagneticField> bFieldHandle_;
  edm::Handle<edm::View<pat::PackedCandidate>> pfCandHandle_;
  edm::Handle<reco::VertexCollection> pvHandle_;

  const AnalyticalImpactPointExtrapolator* impactPointExtrapolator_;

  bool isMC_;

  double ptMinMu_;
  double etaMaxMu_;
  double ptMinKaon_;

  double etaMaxKaon_;
  double DCASigMinKaon_;
  bool   diMuonCharge_;
  double minBKmmMass_;
  double maxBKmmMass_;
  double minBKKmmMass_;
  double maxBKKmmMass_;
  double maxTwoTrackDOCA_;
  BdtReaderData bdtData_;
  TMVA::Reader  bdtReader0_;
  TMVA::Reader  bdtReader1_;
  TMVA::Reader  bdtReader2_;
    
};

BxToMuMuProducer::BxToMuMuProducer(const edm::ParameterSet &iConfig):
beamSpotToken_( consumes<reco::BeamSpot> ( iConfig.getParameter<edm::InputTag>( "beamSpot" ) ) ),
beamSpot_(nullptr),
vertexToken_( consumes<reco::VertexCollection> ( iConfig.getParameter<edm::InputTag>( "vertexCollection" ) ) ),
muonToken_( consumes<std::vector<pat::Muon>> ( iConfig.getParameter<edm::InputTag>( "muonCollection" ) ) ),
pfCandToken_( consumes<edm::View<pat::PackedCandidate>> ( iConfig.getParameter<edm::InputTag>( "PFCandCollection" ) ) ),
prunedGenToken_( consumes<edm::View<reco::GenParticle>> ( iConfig.getParameter<edm::InputTag>( "prunedGenParticleCollection" ) ) ),
prunedGenParticles_(nullptr),
impactPointExtrapolator_(0),
isMC_(            iConfig.getParameter<bool>( "isMC" ) ),
ptMinMu_(         iConfig.getParameter<double>( "MuonMinPt" ) ),
etaMaxMu_(        iConfig.getParameter<double>( "MuonMaxEta" ) ),
ptMinKaon_(       iConfig.getParameter<double>( "KaonMinPt" ) ),
etaMaxKaon_(      iConfig.getParameter<double>( "KaonMaxEta" ) ),
DCASigMinKaon_(   iConfig.getParameter<double>( "KaonMinDCASig" ) ),
diMuonCharge_(    iConfig.getParameter<bool>(   "DiMuonChargeCheck" ) ),
minBKmmMass_(     iConfig.getParameter<double>( "minBKmmMass" ) ),
maxBKmmMass_(     iConfig.getParameter<double>( "maxBKmmMass" ) ),
minBKKmmMass_(    iConfig.getParameter<double>( "minBKKmmMass" ) ),
maxBKKmmMass_(    iConfig.getParameter<double>( "maxBKKmmMass" ) ),
maxTwoTrackDOCA_( iConfig.getParameter<double>( "maxTwoTrackDOCA" ) ),
bdtReader0_("!Color:Silent"),
bdtReader1_("!Color:Silent"),
bdtReader2_("!Color:Silent")
{
    produces<pat::CompositeCandidateCollection>("DiMuon");
    produces<pat::CompositeCandidateCollection>("BToKmumu");
    produces<pat::CompositeCandidateCollection>("BToKKmumu");
    setupTmvaReader(bdtReader0_,(iConfig.getParameter<edm::FileInPath>("bdtEvent0")).fullPath());
    setupTmvaReader(bdtReader1_,(iConfig.getParameter<edm::FileInPath>("bdtEvent1")).fullPath());
    setupTmvaReader(bdtReader2_,(iConfig.getParameter<edm::FileInPath>("bdtEvent2")).fullPath());
}

bool BxToMuMuProducer::isGoodMuon(const pat::Muon& muon){
  if ( not muon.isLooseMuon() ) return false;
  if ( not muon.isTrackerMuon() ) return false;
  if ( not muon.innerTrack()->quality(reco::Track::highPurity) ) return false; 
  if ( muon.pt() < ptMinMu_ || fabs(muon.eta()) > etaMaxMu_ ) return false;
  return true;
}

namespace {
  void addFitInfo(pat::CompositeCandidate& cand, const KinematicFitResult& fit, std::string name, 
		  const DisplacementInformationIn3D& displacement3d = DisplacementInformationIn3D() ){
    cand.addUserInt(   name+"_valid",       fit.valid() );
    cand.addUserFloat( name+"_vtx_prob",    fit.vtxProb() );
    cand.addUserFloat( name+"_vtx_chi2dof", fit.chi2()>0?fit.chi2()/fit.ndof():-1);
    cand.addUserFloat( name+"_mass",        fit.mass() );
    cand.addUserFloat( name+"_massErr",     fit.massErr() );
    cand.addUserFloat( name+"_lxy",         fit.lxy );
    cand.addUserFloat( name+"_sigLxy",      fit.sigLxy );
    cand.addUserFloat( name+"_cosAlphaXY",  fit.cosAlpha );
    cand.addUserFloat( name+"_alpha",       fabs(displacement3d.cosAlpha)<=1?acos(displacement3d.cosAlpha):-999. );
    cand.addUserFloat( name+"_vtx_x",       fit.valid()?fit.refitVertex->position().x():0 );
    cand.addUserFloat( name+"_vtx_xErr",    fit.valid()?sqrt(fit.refitVertex->error().cxx()):0 );
    cand.addUserFloat( name+"_vtx_y",       fit.valid()?fit.refitVertex->position().y():0 );
    cand.addUserFloat( name+"_vtx_yErr",    fit.valid()?sqrt(fit.refitVertex->error().cyy()):0 );
    cand.addUserFloat( name+"_vtx_z",       fit.valid()?fit.refitVertex->position().z():0 );
    cand.addUserFloat( name+"_vtx_zErr",    fit.valid()?sqrt(fit.refitVertex->error().czz()):0 );
    cand.addUserFloat( name+"_pt",          fit.p3().perp() );
    cand.addUserFloat( name+"_eta",         fit.p3().eta() );
    
    // IP info
    cand.addUserFloat( name+"_l3d",         displacement3d.decayLength);
    cand.addUserFloat( name+"_sl3d",        displacement3d.decayLengthErr>0?displacement3d.decayLength/displacement3d.decayLengthErr:0);
    cand.addUserFloat( name+"_pv_z",        displacement3d.pv?displacement3d.pv->position().z():0);
    cand.addUserFloat( name+"_pv_zErr",     displacement3d.pv?displacement3d.pv->zError():0);
    cand.addUserFloat( name+"_pvip",        displacement3d.distaceOfClosestApproach);
    cand.addUserFloat( name+"_pvipErr",     displacement3d.distaceOfClosestApproachErr);
    cand.addUserFloat( name+"_pvlip",       displacement3d.longitudinalImpactParameter);
    cand.addUserFloat( name+"_pvlipErr",    displacement3d.longitudinalImpactParameterErr);
    cand.addUserFloat( name+"_pv2lip",      displacement3d.longitudinalImpactParameter2);
    cand.addUserFloat( name+"_pv2lipErr",   displacement3d.longitudinalImpactParameter2Err);
    cand.addUserInt(   name+"_pvIndex",     displacement3d.pvIndex);

    // Refitted daughter information
    std::vector<GlobalVector> muons;
    std::vector<GlobalVector> kaons;
    for (auto dau: fit.refitDaughters){
      if (fabs(dau->currentState().mass()-MuonMass_)<0.001)
	muons.push_back(dau->currentState().globalMomentum());
      if (fabs(dau->currentState().mass()-KaonMass_)<0.001)
	kaons.push_back(dau->currentState().globalMomentum());
    }
    std::cout << "Daughter info availability for " << name << " :" << std::endl;
    std::cout << "\tmuons: " << muons.size() << std::endl;
    std::cout << "\tkaons: " << kaons.size() << std::endl;
    if (muons.size()==2){
      cand.addUserFloat( name+"_mu1pt",       muons.at(0).perp() );
      cand.addUserFloat( name+"_mu1eta",      muons.at(0).eta() );
      cand.addUserFloat( name+"_mu1phi",      muons.at(0).phi() );
      cand.addUserFloat( name+"_mu2pt",       muons.at(1).perp() );
      cand.addUserFloat( name+"_mu2eta",      muons.at(1).eta() );
      cand.addUserFloat( name+"_mu2phi",      muons.at(1).phi() );
    }
    if (kaons.size()>0){
      cand.addUserFloat( name+"_kaon1pt",       kaons.at(0).perp() );
      cand.addUserFloat( name+"_kaon1eta",      kaons.at(0).eta() );
      cand.addUserFloat( name+"_kaon1phi",      kaons.at(0).phi() );
      if (kaons.size()>1){
	cand.addUserFloat( name+"_kaon2pt",       kaons.at(1).perp() );
	cand.addUserFloat( name+"_kaon2eta",      kaons.at(1).eta() );
	cand.addUserFloat( name+"_kaon2phi",      kaons.at(1).phi() );
      }
    }
  }
}

CloseTrackInfo 
BxToMuMuProducer::findTracksCompatibleWithTheVertex(const pat::Muon& muon1,
						    const pat::Muon& muon2,
						    const KinematicFitResult& fit, 
						    double maxDoca,
						    std::vector<const pat::PackedCandidate*> ignoreTracks)
{
  CloseTrackInfo result;
  if (not fit.valid()) return result;
  for (const auto& pfCand: *pfCandHandle_.product()){
    bool ignore_track = false;
    for (auto trk: ignoreTracks){
      if (trk==&pfCand){
	ignore_track = true;
	break;
      }
    }
    if (ignore_track) continue;
    
    if (deltaR(muon1, pfCand) < 0.01 || deltaR(muon2, pfCand) < 0.01) continue;
    if (pfCand.charge() == 0 ) continue;
    if (!pfCand.hasTrackDetails()) continue;
    double mu1_kaon_doca = distanceOfClosestApproach(muon1.innerTrack().get(),
						     pfCand.bestTrack());
    double mu2_kaon_doca = distanceOfClosestApproach(muon2.innerTrack().get(),
						     pfCand.bestTrack());
    if (mu1_kaon_doca>maxDoca or mu2_kaon_doca>maxDoca) continue;
    
    CloseTrack track;
    track.pfCand = &pfCand;
    auto doca = distanceOfClosestApproach(pfCand.bestTrack(),fit.refitVertex);
    track.svDoca = doca.value();
    track.svDocaErr = doca.error();

    // add PV doca
    if (pfCand.vertexRef().key()<pvHandle_->size()){
      doca = distanceOfClosestApproach(pfCand.bestTrack(),pvHandle_->at(pfCand.vertexRef().key()) );
      track.pvDoca = doca.value();
      track.pvDocaErr = doca.error();
    }
    
    auto fit_result = vertexWithKinematicFitter(muon1, muon2, pfCand);
    if (fit_result.valid()){
      track.svProb = fit_result.vtxProb();
      track.impactParameterSignificanceBS = pfCand.bestTrack()->dxyError()>0 ? fabs(pfCand.bestTrack()->dxy(*beamSpot_))/pfCand.bestTrack()->dxyError():0.0;
    }
    result.tracks.push_back(track);
  }

  return result;
}

float
BxToMuMuProducer::computeTrkMuonIsolation(const pat::Muon& the_muon, const pat::Muon& the_other_muon, 
					  unsigned int primaryVertexIndex,
					  float minPt, float dR,
					  std::vector<const pat::PackedCandidate*> ignoreTracks)
{
  float sumPt(0);
  for (const auto& pfCand: *pfCandHandle_.product()){
    bool ignore_track = false;
    for (auto trk: ignoreTracks){
      if (trk==&pfCand){
	ignore_track = true;
	break;
      }
    }
    if (ignore_track) continue;
    if (deltaR(the_muon, pfCand) < 0.01 || deltaR(the_other_muon, pfCand) < 0.01) continue;
    if (pfCand.charge() == 0 ) continue;
    if (!pfCand.hasTrackDetails()) continue;
    if (pfCand.pt()<minPt) continue;
    if (pfCand.vertexRef().key()!=primaryVertexIndex) continue;
    if (deltaR(the_muon, pfCand) > dR) continue;
    sumPt += pfCand.pt();
  }

  return the_muon.pt()/(the_muon.pt()+sumPt);
}

float
BxToMuMuProducer::computeTrkMuMuIsolation(const pat::Muon& muon1, const pat::Muon& muon2, 
					  unsigned int primaryVertexIndex,
					  float minPt, float dR,
					  std::vector<const pat::PackedCandidate*> ignoreTracks)
{
  float sumPt(0);
  auto b_p4 = muon1.p4()+muon2.p4();
  for (const auto& pfCand: *pfCandHandle_.product()){
    bool ignore_track = false;
    for (auto trk: ignoreTracks){
      if (trk==&pfCand){
	ignore_track = true;
	break;
      }
    }
    if (ignore_track) continue;
    if (deltaR(muon1, pfCand) < 0.01 || deltaR(muon2, pfCand) < 0.01) continue;
    if (pfCand.charge() == 0 ) continue;
    if (!pfCand.hasTrackDetails()) continue;
    if (pfCand.pt()<minPt) continue;
    if (pfCand.vertexRef().key()!=primaryVertexIndex) continue;
    if (deltaR(b_p4, pfCand) > dR) continue;
    sumPt += pfCand.pt();
  }

  return b_p4.pt()/(b_p4.pt()+sumPt);
}


float
BxToMuMuProducer::otherVertexMaxProb(const pat::Muon& muon1, 
				     const pat::Muon& muon2,
				     float minPt,
				     float max_doca,
				     std::vector<const pat::PackedCandidate*> ignoreTracks){
  float bestMu1Vtx = 0;
  float bestMu2Vtx = 0;
  KalmanVertexFitter kvf;
  std::vector<reco::TransientTrack> transTrksForMu1Vertex;
  transTrksForMu1Vertex.push_back((*theTTBuilder_).build(muon1.innerTrack().get()));
  std::vector<reco::TransientTrack> transTrksForMu2Vertex;
  transTrksForMu2Vertex.push_back((*theTTBuilder_).build(muon2.innerTrack().get()));


  for (const auto& pfCand: *pfCandHandle_.product()){
    bool ignore_track = false;
    for (auto trk: ignoreTracks){
      if (trk==&pfCand){
	ignore_track = true;
	break;
      }
    }
    if (ignore_track) continue;
    if (deltaR(muon1, pfCand) < 0.01 || deltaR(muon2, pfCand) < 0.01) continue;
    if (pfCand.charge() == 0 ) continue;
    if (!pfCand.hasTrackDetails()) continue;
    if (pfCand.pt()<minPt) continue;
    double mu1_doca = distanceOfClosestApproach(muon1.innerTrack().get(),
						pfCand.bestTrack());
    double mu2_doca = distanceOfClosestApproach(muon2.innerTrack().get(),
						pfCand.bestTrack());
    if (mu1_doca < max_doca and mu1_doca < mu2_doca){
      // first  muon is closer - check vertex probability
      transTrksForMu1Vertex.push_back((*theTTBuilder_).build(pfCand.bestTrack()));
      TransientVertex tv = kvf.vertex(transTrksForMu1Vertex);
      if ( tv.isValid() ){
	float vtxProb = TMath::Prob(tv.totalChiSquared(), (int)tv.degreesOfFreedom());
	if (vtxProb > bestMu1Vtx) bestMu1Vtx = vtxProb;
      }
      transTrksForMu1Vertex.pop_back();
    }
    if (mu2_doca < max_doca and mu2_doca < mu1_doca){
      // second  muon is closer - check vertex probability
      transTrksForMu2Vertex.push_back((*theTTBuilder_).build(pfCand.bestTrack()));
      TransientVertex tv = kvf.vertex(transTrksForMu2Vertex);
      if ( tv.isValid() ){
	float vtxProb = TMath::Prob(tv.totalChiSquared(), (int)tv.degreesOfFreedom());
	if (vtxProb > bestMu2Vtx) bestMu2Vtx = vtxProb;
      }
      transTrksForMu2Vertex.pop_back();
    }
  }
  return max(bestMu1Vtx,bestMu2Vtx);
}

void BxToMuMuProducer::fillMuMuInfo(pat::CompositeCandidate& dimuonCand,
				    const edm::Event& iEvent,
				    const KinematicFitResult& kinematicMuMuVertexFit,
				    const pat::Muon& muon1,
				    const pat::Muon& muon2
				    ) 
{
  // printf("kinematicMuMuVertexFit (x,y,z): (%7.3f,%7.3f,%7.3f)\n", 
  // 	 kinematicMuMuVertexFit.refitVertex->position().x(),
  // 	 kinematicMuMuVertexFit.refitVertex->position().y(),
  // 	 kinematicMuMuVertexFit.refitVertex->position().z());
  auto displacement3D = compute3dDisplacement(kinematicMuMuVertexFit, *pvHandle_.product(),true);
  addFitInfo(dimuonCand, kinematicMuMuVertexFit, "kin", displacement3D);
  
  if (isMC_){
    auto gen_mm = getGenMatchInfo(*prunedGenParticles_,muon1,muon2);
    // int mu1_pdgId, mu1_motherPdgId, mu2_pdgId, mu2_motherPdgId, kaon_pdgId, kaon_motherPdgId,
    // mm_pdgId, mm_motherPdgId, kmm_pdgId;
    // float mu1_pt, mu2_pt, kaon_pt, mm_mass, mm_pt, kmm_mass, kmm_pt;
    dimuonCand.addUserInt(  "gen_mu1_pdgId",   gen_mm.mu1_pdgId);
    dimuonCand.addUserInt(  "gen_mu1_mpdgId",  gen_mm.mu1_motherPdgId);
    dimuonCand.addUserFloat("gen_mu1_pt",      gen_mm.mu1_pt);
    dimuonCand.addUserInt(  "gen_mu2_pdgId",   gen_mm.mu2_pdgId);
    dimuonCand.addUserInt(  "gen_mu2_mpdgId",  gen_mm.mu2_motherPdgId);
    dimuonCand.addUserFloat("gen_mu2_pt",      gen_mm.mu2_pt);
    dimuonCand.addUserFloat("gen_mass",        gen_mm.mm_mass);
    dimuonCand.addUserFloat("gen_pt",          gen_mm.mm_pt);
    dimuonCand.addUserInt(  "gen_pdgId",       gen_mm.mm_pdgId);
    dimuonCand.addUserInt(  "gen_mpdgId",      gen_mm.mm_motherPdgId);
    dimuonCand.addUserFloat("gen_prod_x",      gen_mm.mm_prod_vtx.x());
    dimuonCand.addUserFloat("gen_prod_y",      gen_mm.mm_prod_vtx.y());
    dimuonCand.addUserFloat("gen_prod_z",      gen_mm.mm_prod_vtx.z());
    dimuonCand.addUserFloat("gen_vtx_x",       gen_mm.mm_vtx.x());
    dimuonCand.addUserFloat("gen_vtx_y",       gen_mm.mm_vtx.y());
    dimuonCand.addUserFloat("gen_vtx_z",       gen_mm.mm_vtx.z());
    dimuonCand.addUserFloat("gen_l3d",         (gen_mm.mm_prod_vtx-gen_mm.mm_vtx).r());
    dimuonCand.addUserFloat("gen_lxy",         (gen_mm.mm_prod_vtx-gen_mm.mm_vtx).rho());
  }

  int pvIndex = displacement3D.pvIndex;

  // Look for additional tracks compatible with the dimuon vertex
  auto closeTracks = findTracksCompatibleWithTheVertex(muon1,muon2,kinematicMuMuVertexFit);
  closeTracks.fillCandInfo(dimuonCand, pvIndex, "");

  dimuonCand.addUserFloat( "m1iso",     computeTrkMuonIsolation(muon1,muon2,pvIndex,0.5,0.5));
  dimuonCand.addUserFloat( "m2iso",     computeTrkMuonIsolation(muon2,muon1,pvIndex,0.5,0.5));
  dimuonCand.addUserFloat( "iso",       computeTrkMuMuIsolation(muon2,muon1,pvIndex,0.9,0.7));
  dimuonCand.addUserFloat( "otherVtxMaxProb", otherVertexMaxProb(muon1,muon2,0.5));
  dimuonCand.addUserFloat( "otherVtxMaxProb1", otherVertexMaxProb(muon1,muon2,1.0));
  dimuonCand.addUserFloat( "otherVtxMaxProb2", otherVertexMaxProb(muon1,muon2,2.0));

  // BDT
  bdtData_.fls3d    = dimuonCand.userFloat("kin_sl3d");
  bdtData_.alpha    = dimuonCand.userFloat("kin_alpha");
  bdtData_.pvips    = dimuonCand.userFloat("kin_pvip")/dimuonCand.userFloat("kin_pvipErr");
  bdtData_.iso      = dimuonCand.userFloat("iso");
  bdtData_.chi2dof  = dimuonCand.userFloat("kin_vtx_chi2dof");
  bdtData_.docatrk  = dimuonCand.userFloat("docatrk");
  bdtData_.closetrk = dimuonCand.userInt(  "closetrk");
  bdtData_.m1iso    = dimuonCand.userFloat("m1iso");
  bdtData_.m2iso    = dimuonCand.userFloat("m2iso");
  bdtData_.eta      = dimuonCand.userFloat("kin_eta");	  
  bdtData_.m        = dimuonCand.userFloat("kin_mass");	  

  dimuonCand.addUserFloat("bdt",computeAnalysisBDT(iEvent.eventAuxiliary().event()%3));
}

void BxToMuMuProducer::fillBtoJpsiKInfo(pat::CompositeCandidate& btokmmCand,
					const edm::Event& iEvent,
					const KinematicFitResult& kinematicMuMuVertexFit,
					const pat::Muon& muon1,
					const pat::Muon& muon2,
					const pat::PackedCandidate & kaon
					) 
{
  btokmmCand.addUserFloat("kaon_pt",     kaon.pt());
  btokmmCand.addUserFloat("kaon_eta",    kaon.eta());
  btokmmCand.addUserFloat("kaon_phi",    kaon.phi());
  btokmmCand.addUserFloat("kaon_dxy_bs", kaon.bestTrack()->dxy(*beamSpot_));
  btokmmCand.addUserFloat("kaon_sdxy_bs", 
			  kaon.bestTrack()->dxyError()>0 ? fabs(kaon.bestTrack()->dxy(*beamSpot_))/kaon.bestTrack()->dxyError():0.0);
  btokmmCand.addUserInt("kaon_charge", kaon.charge());
  if (isMC_){
    auto gen_kmm = getGenMatchInfo(*prunedGenParticles_,muon1,muon2,&kaon);
    btokmmCand.addUserInt(  "gen_kaon_pdgId",  gen_kmm.kaon1_pdgId);
    btokmmCand.addUserInt(  "gen_kaon_mpdgId", gen_kmm.kaon1_motherPdgId);
    btokmmCand.addUserFloat("gen_kaon_pt",     gen_kmm.kaon1_pt);
    btokmmCand.addUserFloat("gen_mass",        gen_kmm.kmm_mass);
    btokmmCand.addUserFloat("gen_pt",          gen_kmm.kmm_pt);
    btokmmCand.addUserInt(  "gen_pdgId",       gen_kmm.kmm_pdgId);
    btokmmCand.addUserFloat("gen_prod_x",      gen_kmm.kmm_prod_vtx.x());
    btokmmCand.addUserFloat("gen_prod_y",      gen_kmm.kmm_prod_vtx.y());
    btokmmCand.addUserFloat("gen_prod_z",      gen_kmm.kmm_prod_vtx.z());
    btokmmCand.addUserFloat("gen_l3d",         (gen_kmm.kmm_prod_vtx-gen_kmm.mm_vtx).r());
    btokmmCand.addUserFloat("gen_lxy",         (gen_kmm.kmm_prod_vtx-gen_kmm.mm_vtx).rho());
  }

  // if (kaon.genParticle()){
  // 	btokmmCand.addUserInt("kaon_mc_pdgId", kaon.genParticle().pdgId());
  // } else {
  // 	btokmmCand.addUserInt("kaon_mc_pdgId", 0);
  // }
  
  auto bToKJPsiMuMu_NoMassConstraint = fitBToKJPsiMuMu(kinematicMuMuVertexFit.refitMother, kaon, false);
  bToKJPsiMuMu_NoMassConstraint.postprocess(*beamSpot_);
  auto bToKJPsiMuMu_NoMassConstraint_displacement = compute3dDisplacement(bToKJPsiMuMu_NoMassConstraint, *pvHandle_.product(),true);
  addFitInfo(btokmmCand, bToKJPsiMuMu_NoMassConstraint, "nomc", bToKJPsiMuMu_NoMassConstraint_displacement);
  
  // worse performing option
  // auto bToKJPsiMuMuWithMassConstraint = fitBToKJPsiMuMu(kinematicMuMuVertexFit.refitMother, kaon, true);
  // bToKJPsiMuMuWithMassConstraint.postprocess(beamSpot);
  // addFitInfo(btokmmCand, bToKJPsiMuMuWithMassConstraint, "jpsimc");
  
  auto bToKJPsiMuMu_MassConstraint = fitBToKJPsiMuMuNew(kinematicMuMuVertexFit.refitTree, kaon, true);
  bToKJPsiMuMu_MassConstraint.postprocess(*beamSpot_);
  auto bToKJPsiMuMu_MassConstraint_displacement = compute3dDisplacement(bToKJPsiMuMu_MassConstraint, *pvHandle_.product(),true);
  addFitInfo(btokmmCand, bToKJPsiMuMu_MassConstraint, "jpsimc", bToKJPsiMuMu_MassConstraint_displacement);
  
  // broken pointing constraint
  // auto bToKJPsiMuMu_MC_PC = refitWithPointingConstraint(bToKJPsiMuMu_MC.refitTree, primaryVertex);
  // bToKJPsiMuMu_MC_PC.postprocess(beamSpot);
  // addFitInfo(btokmmCand, bToKJPsiMuMu_MC_PC, "mcpc");
}

void BxToMuMuProducer::fillBstoJpsiKKInfo(pat::CompositeCandidate& bCand,
					  const edm::Event& iEvent,
					  const KinematicFitResult& kinematicMuMuVertexFit,
					  const pat::Muon& muon1,
					  const pat::Muon& muon2,
					  const pat::PackedCandidate & kaon1,
					  const pat::PackedCandidate & kaon2
					) 
{
  bCand.addUserFloat("kaon1_pt",     kaon1.pt());
  bCand.addUserFloat("kaon1_eta",    kaon1.eta());
  bCand.addUserFloat("kaon1_phi",    kaon1.phi());
  bCand.addUserFloat("kaon1_dxy_bs", kaon1.bestTrack()->dxy(*beamSpot_));
  bCand.addUserFloat("kaon1_sdxy_bs", 
			  kaon1.bestTrack()->dxyError()>0 ? fabs(kaon1.bestTrack()->dxy(*beamSpot_))/kaon1.bestTrack()->dxyError():0.0);
  bCand.addUserInt("kaon1_charge", kaon1.charge());

  bCand.addUserFloat("kaon2_pt",     kaon2.pt());
  bCand.addUserFloat("kaon2_eta",    kaon2.eta());
  bCand.addUserFloat("kaon2_phi",    kaon2.phi());
  bCand.addUserFloat("kaon2_dxy_bs", kaon2.bestTrack()->dxy(*beamSpot_));
  bCand.addUserFloat("kaon2_sdxy_bs", 
			  kaon2.bestTrack()->dxyError()>0 ? fabs(kaon2.bestTrack()->dxy(*beamSpot_))/kaon2.bestTrack()->dxyError():0.0);
  bCand.addUserInt("kaon2_charge", kaon2.charge());
  bCand.addUserFloat("kk_mass",    (kaon1.p4()+kaon2.p4()).mass());

  if (isMC_){
    auto gen_info = getGenMatchInfo(*prunedGenParticles_,muon1,muon2,&kaon1,&kaon2);
    bCand.addUserInt(  "gen_kaon1_pdgId",  gen_info.kaon1_pdgId);
    bCand.addUserInt(  "gen_kaon1_mpdgId", gen_info.kaon1_motherPdgId);
    bCand.addUserFloat("gen_kaon1_pt",     gen_info.kaon1_pt);
    bCand.addUserInt(  "gen_kaon2_pdgId",  gen_info.kaon2_pdgId);
    bCand.addUserInt(  "gen_kaon2_mpdgId", gen_info.kaon2_motherPdgId);
    bCand.addUserFloat("gen_kaon2_pt",     gen_info.kaon2_pt);
    bCand.addUserFloat("gen_mass",         gen_info.kkmm_mass);
    bCand.addUserFloat("gen_pt",           gen_info.kkmm_pt);
    bCand.addUserInt(  "gen_pdgId",        gen_info.kkmm_pdgId);
    bCand.addUserFloat("gen_prod_x",       gen_info.kkmm_prod_vtx.x());
    bCand.addUserFloat("gen_prod_y",       gen_info.kkmm_prod_vtx.y());
    bCand.addUserFloat("gen_prod_z",       gen_info.kkmm_prod_vtx.z());
    bCand.addUserFloat("gen_l3d",         (gen_info.kkmm_prod_vtx-gen_info.mm_vtx).r());
    bCand.addUserFloat("gen_lxy",         (gen_info.kkmm_prod_vtx-gen_info.mm_vtx).rho());
  }

  auto bToKKJPsiMuMu = fitBToKKMuMu(kinematicMuMuVertexFit.refitTree, kaon1, kaon2, true);
  bToKKJPsiMuMu.postprocess(*beamSpot_);
  auto bToKKJPsiMuMu_displacement = compute3dDisplacement(bToKKJPsiMuMu, *pvHandle_.product(),true);
  addFitInfo(bCand, bToKKJPsiMuMu, "jpsikk", bToKKJPsiMuMu_displacement);
  bCand.addUserFloat("jpsikk_kk_mass",    bToKKJPsiMuMu.refit_mass(1,2));
}


void BxToMuMuProducer::fillBDTForBtoJpsiKThatEmulatesBmm(pat::CompositeCandidate& btokmmCand,
							 const pat::CompositeCandidate& dimuonCand,
							 const edm::Event& iEvent,
							 const KinematicFitResult& kinematicMuMuVertexFit,
							 const pat::Muon& muon1,
							 const pat::Muon& muon2,
							 const pat::PackedCandidate & kaon
					) 
{
  ///////
  //// Treat B->JpsiK as B->mm for signal studies in data
  //
  std::vector<const pat::PackedCandidate*> ignoreTracks;
  ignoreTracks.push_back(&kaon);

  int pvIndex = btokmmCand.userInt("jpsimc_pvIndex");

  // Look for additional tracks compatible with the dimuon vertex
  auto closeTracks = findTracksCompatibleWithTheVertex(muon1,muon2,kinematicMuMuVertexFit,0.03,ignoreTracks);
  closeTracks.fillCandInfo(btokmmCand, pvIndex, "bmm");

  btokmmCand.addUserFloat( "bmm_m1iso",     computeTrkMuonIsolation(muon1,muon2,pvIndex,0.5,0.5,ignoreTracks));
  btokmmCand.addUserFloat( "bmm_m2iso",     computeTrkMuonIsolation(muon2,muon1,pvIndex,0.5,0.5,ignoreTracks));
  btokmmCand.addUserFloat( "bmm_iso",       computeTrkMuMuIsolation(muon2,muon1,pvIndex,0.9,0.7,ignoreTracks));
  btokmmCand.addUserFloat( "bmm_otherVtxMaxProb",  otherVertexMaxProb(muon1,muon2,0.5,0.1,ignoreTracks));
  btokmmCand.addUserFloat( "bmm_otherVtxMaxProb1", otherVertexMaxProb(muon1,muon2,1.0,0.1,ignoreTracks));
  btokmmCand.addUserFloat( "bmm_otherVtxMaxProb2", otherVertexMaxProb(muon1,muon2,2.0,0.1,ignoreTracks));

  // BDT
  bdtData_.fls3d    = dimuonCand.userFloat("kin_sl3d");
  bdtData_.alpha    = btokmmCand.userFloat("jpsimc_alpha");
  bdtData_.pvips    = btokmmCand.userFloat("jpsimc_pvip")/btokmmCand.userFloat("jpsimc_pvipErr");
  // One can use bkmm without mass constraint, but it doesn't help
  // bdtData_.alpha    = btokmmCand.userFloat("nomc_alpha");
  // bdtData_.pvips    = btokmmCand.userFloat("nomc_pvip")/btokmmCand.userFloat("nomc_pvipErr");
  bdtData_.iso      = btokmmCand.userFloat("bmm_iso");
  bdtData_.chi2dof  = dimuonCand.userFloat("kin_vtx_chi2dof");
  bdtData_.docatrk  = btokmmCand.userFloat("bmm_docatrk");
  bdtData_.closetrk = btokmmCand.userInt(  "bmm_closetrk");
  bdtData_.m1iso    = btokmmCand.userFloat("bmm_m1iso");
  bdtData_.m2iso    = btokmmCand.userFloat("bmm_m2iso");
  bdtData_.eta      = btokmmCand.userFloat("jpsimc_eta");	  
  bdtData_.m        = btokmmCand.userFloat("jpsimc_mass");	  

  btokmmCand.addUserFloat("bmm_bdt",computeAnalysisBDT(iEvent.eventAuxiliary().event()%3));

}

void BxToMuMuProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {

    iSetup.get<IdealMagneticFieldRecord>().get(bFieldHandle_);
    iSetup.get<TransientTrackRecord>().get("TransientTrackBuilder",theTTBuilder_);

    AnalyticalImpactPointExtrapolator extrapolator(bFieldHandle_.product());
    impactPointExtrapolator_ = &extrapolator;

    edm::Handle<reco::BeamSpot> beamSpotHandle;
    
    iEvent.getByToken(beamSpotToken_, beamSpotHandle);
    
    if ( ! beamSpotHandle.isValid() ) {
        edm::LogError("BxToMuMuProducer") << "No beam spot available from EventSetup" ;
    }
    
    beamSpot_ = beamSpotHandle.product();
    
    iEvent.getByToken(vertexToken_, pvHandle_);
    // const reco::Vertex & primaryVertex = vertexHandle->front();

    edm::Handle<std::vector<pat::Muon>> muonHandle;
    edm::Handle<edm::View<pat::PackedCandidate>> lostTrackHandle;
    
    iEvent.getByToken(muonToken_, muonHandle);
    iEvent.getByToken(pfCandToken_, pfCandHandle_);
    
    edm::Handle<edm::View<reco::GenParticle> > prunedGenParticleHandle;
    if ( isMC_ ) {
      iEvent.getByToken(prunedGenToken_,prunedGenParticleHandle);
      prunedGenParticles_ = prunedGenParticleHandle.product();
    } else {
      prunedGenParticles_ = nullptr;
    }

    auto nMuons = muonHandle->size();
    auto nPFCands = pfCandHandle_->size();
    // unsigned int lostTrackNumber = useLostTracks_ ? lostTrackHandle->size() : 0;
    
    // Output collection
    auto dimuon  = std::make_unique<pat::CompositeCandidateCollection>();
    auto btokmm  = std::make_unique<pat::CompositeCandidateCollection>();
    auto btokkmm = std::make_unique<pat::CompositeCandidateCollection>();
    AddFourMomenta addP4;

    // Build dimuon candidates first

    if ( nMuons > 1 ){
      // Ensure that muon1.pt > muon2.pt
      for (unsigned int i = 0; i < nMuons; ++i) {
	const pat::Muon & muon1 = muonHandle->at(i);
        if (not isGoodMuon(muon1)) continue;
	for (unsigned int j = 0; j < nMuons; ++j) {
	  if (i==j) continue;
	  const pat::Muon & muon2 = muonHandle->at(j);
	  if (muon2.pt() > muon1.pt()) continue;
	  if (not isGoodMuon(muon2)) continue;
	  
	  auto mm_doca = distanceOfClosestApproach(muon1.innerTrack().get(),
						   muon2.innerTrack().get());
	  if (maxTwoTrackDOCA_>0 and mm_doca > maxTwoTrackDOCA_) continue;
	  if (diMuonCharge_ && muon1.charge()*muon2.charge()>0) continue;

	  pat::CompositeCandidate dimuonCand;
	  dimuonCand.addDaughter( muon1 , "muon1");
	  dimuonCand.addDaughter( muon2 , "muon2");
	  addP4.set( dimuonCand );

	  dimuonCand.addUserInt("mu1_index", i);
	  dimuonCand.addUserInt("mu2_index", j);
	  dimuonCand.addUserFloat( "doca", mm_doca);

	  // Kalman Vertex Fit
	  auto kalmanMuMuVertexFit = vertexMuonsWithKalmanFitter(muon1, muon2);
	  kalmanMuMuVertexFit.postprocess(*beamSpot_);
	  dimuonCand.addUserInt(   "kalman_valid",    kalmanMuMuVertexFit.valid);
	  dimuonCand.addUserFloat( "kalman_vtx_prob", kalmanMuMuVertexFit.vtxProb);
	  dimuonCand.addUserFloat( "kalman_mass",     kalmanMuMuVertexFit.mass() );
	  dimuonCand.addUserFloat( "kalman_lxy",      kalmanMuMuVertexFit.lxy );
	  dimuonCand.addUserFloat( "kalman_sigLxy",   kalmanMuMuVertexFit.sigLxy );
	  
	  // Kinematic Fits
	  auto kinematicMuMuVertexFit = vertexMuonsWithKinematicFitter(muon1, muon2);
	  kinematicMuMuVertexFit.postprocess(*beamSpot_);
	  
	  fillMuMuInfo(dimuonCand,iEvent,kinematicMuMuVertexFit,muon1,muon2);

	  auto imm = dimuon->size();
	  for (unsigned int k = 0; k < nPFCands; ++k) {
	    pat::PackedCandidate kaonCand1((*pfCandHandle_)[k]);
	    kaonCand1.setMass(KaonMass_);
	    if (deltaR(muon1, kaonCand1) < 0.01 || deltaR(muon2, kaonCand1) < 0.01) continue;
	    if (kaonCand1.charge() == 0 ) continue;
	    if (!kaonCand1.hasTrackDetails()) continue;
	    double mu1_kaon_doca = distanceOfClosestApproach(muon1.innerTrack().get(),
							     kaonCand1.bestTrack());
	    double mu2_kaon_doca = distanceOfClosestApproach(muon2.innerTrack().get(),
							     kaonCand1.bestTrack());
	    // BtoJpsiK
	    bool goodBtoJpsiK = true;
	    if (fabs(kinematicMuMuVertexFit.mass()-3.1)>0.2) goodBtoJpsiK = false;
	    if (abs(kaonCand1.pdgId())!=211) goodBtoJpsiK = false; //Charged hadrons
	    if (kaonCand1.pt()<ptMinKaon_ || abs(kaonCand1.eta())>etaMaxKaon_) goodBtoJpsiK = false;
	    if (maxTwoTrackDOCA_>0 and mu1_kaon_doca> maxTwoTrackDOCA_) goodBtoJpsiK = false;	      
	    if (maxTwoTrackDOCA_>0 and mu2_kaon_doca> maxTwoTrackDOCA_) goodBtoJpsiK = false;	      

	    // BToJpsiKK
	    bool goodBtoJpsiKK = goodBtoJpsiK;

	    double kmm_mass = (muon1.p4()+muon2.p4()+kaonCand1.p4()).mass();
	    if ( kmm_mass<minBKmmMass_ || kmm_mass>maxBKmmMass_ ) goodBtoJpsiK = false;

	    
	    if (goodBtoJpsiK){
	      // fill BtoJpsiK candidate info
	    
	      pat::CompositeCandidate btokmmCand;
	      btokmmCand.addUserInt("mm_index", imm);
	      btokmmCand.addUserFloat("kaon_mu1_doca", mu1_kaon_doca);
	      btokmmCand.addUserFloat("kaon_mu2_doca", mu2_kaon_doca);
	      
	      fillBtoJpsiKInfo(btokmmCand,iEvent,kinematicMuMuVertexFit,muon1,muon2,kaonCand1);
	      fillBDTForBtoJpsiKThatEmulatesBmm(btokmmCand,dimuonCand,iEvent,kinematicMuMuVertexFit,muon1,muon2,kaonCand1);

	      btokmm->push_back(btokmmCand);
	    }
	    // else{
	    // New methods of counting tracks
	    // if (maxTwoTrackDOCA_>0 and mu1_kaon_doca<maxTwoTrackDOCA_ and mu2_kaon_doca<maxTwoTrackDOCA_){
	    // 	auto fit_result = vertexWithKinematicFitter(muon1, muon2, kaonCand1);
	    // 	if ( fit_result.vtxProb()>0.1 ) {
	    // 	  nTracksCompatibleWithTheMuMuVertex++;
	    // 	  double sigDxy = kaonCand1.bestTrack()->dxyError()>0 ? fabs(kaonCand1.bestTrack()->dxy(beamSpot))/kaonCand1.bestTrack()->dxyError():0.0;
	    // 	  if (sigDxy>2.0) nDisplacedTracksCompatibleWithTheMuMuVertex++;
	    // 	}
	    // }	
	    //
	    //  continue;
	    // }

	    if (goodBtoJpsiKK){ // good candidate to consider for JpsiKK
	      for (unsigned int k2 = k+1; k2 < nPFCands; ++k2) { // only works if selection requirements for both kaons are identical
		pat::PackedCandidate kaonCand2((*pfCandHandle_)[k2]);
		kaonCand2.setMass(KaonMass_);
		if (deltaR(muon1, kaonCand2) < 0.01 || deltaR(muon2, kaonCand2) < 0.01) continue;
		if (kaonCand2.charge() == 0 ) continue;
		if (!kaonCand2.hasTrackDetails()) continue;
		double mu1_kaon2_doca = distanceOfClosestApproach(muon1.innerTrack().get(),
							     kaonCand2.bestTrack());
		double mu2_kaon2_doca = distanceOfClosestApproach(muon2.innerTrack().get(),
							     kaonCand2.bestTrack());
	      
		if (abs(kaonCand2.pdgId())!=211) goodBtoJpsiKK = false; //Charged hadrons
		if (kaonCand2.pt()<ptMinKaon_ || abs(kaonCand2.eta())>etaMaxKaon_) goodBtoJpsiKK = false;
		if (maxTwoTrackDOCA_>0 and mu1_kaon2_doca> maxTwoTrackDOCA_) goodBtoJpsiKK = false;	      
		if (maxTwoTrackDOCA_>0 and mu2_kaon2_doca> maxTwoTrackDOCA_) goodBtoJpsiKK = false;	      

		double kkmm_mass = (muon1.p4()+muon2.p4()+kaonCand1.p4()+kaonCand2.p4()).mass();
		if ( kkmm_mass<minBKKmmMass_ || kkmm_mass>maxBKKmmMass_ ) goodBtoJpsiKK = false;
		
		if (goodBtoJpsiKK){
		  // fill BtoJpsiKK candidate info
		  
		  pat::CompositeCandidate btokkmmCand;
		  btokkmmCand.addUserInt("mm_index", imm);
		  int ikmm = -1;
		  if (goodBtoJpsiK) ikmm = btokmm->size()-1;
		  btokkmmCand.addUserInt("kmm_index", ikmm);
		  btokkmmCand.addUserFloat("kaon_mu1_doca", mu1_kaon2_doca);
		  btokkmmCand.addUserFloat("kaon_mu2_doca", mu2_kaon2_doca);
	      
		  fillBstoJpsiKKInfo(btokkmmCand,iEvent,kinematicMuMuVertexFit,muon1,muon2,kaonCand1,kaonCand2);
		  // FIXME
		  // fillBDTForBtoJpsiKThatEmulatesBmm(btokkmmCand,dimuonCand,iEvent,kinematicMuMuVertexFit,muon1,muon2,kaonCand1);

		  btokkmm->push_back(btokkmmCand);
		}
	      }
	    }
	  }                  

	  dimuon->push_back(dimuonCand);
	}
      }
    }
    
    iEvent.put(std::move(dimuon), "DiMuon");
    iEvent.put(std::move(btokmm), "BToKmumu");
    iEvent.put(std::move(btokkmm),"BToKKmumu");
    
}

KalmanVertexFitResult 
BxToMuMuProducer::vertexWithKalmanFitter(std::vector<const reco::Track*> trks, 
					 std::vector<float> masses){
  if (trks.size()!=masses.size()) 
    throw cms::Exception("Error") << "number of tracks and number of masses should match";
  KalmanVertexFitResult results;
  std::vector<reco::TransientTrack> transTrks;
  for (auto trk: trks){
    transTrks.push_back((*theTTBuilder_).build(trk));
  }
  KalmanVertexFitter kvf(true);
  TransientVertex tv = kvf.vertex(transTrks);

  if ( tv.isValid() ){
    results.vtxProb = TMath::Prob(tv.totalChiSquared(), (int)tv.degreesOfFreedom());
    results.valid = true;
    results.position = tv.position();
    results.err = tv.positionError();
    if (tv.hasRefittedTracks()){
      assert(tv.refittedTracks().size()==transTrks.size());
      for (unsigned int i=0; i<transTrks.size(); ++i){
	// Is it safe to assume that the order hasn't changed?
	GlobalVector gvP = tv.refittedTracks()[i].trajectoryStateClosestToPoint(tv.position()).momentum();
	// GlobalVector gvP = tv.originalTracks()[i].trajectoryStateClosestToPoint(tv.position()).momentum();
	results.refitVectors.push_back(makeLorentzVectorFromPxPyPzM(gvP.x(), gvP.y(), gvP.z(), masses[i]));
      }
    }
  }
  return results;
}

KalmanVertexFitResult 
BxToMuMuProducer::vertexMuonsWithKalmanFitter(const pat::Muon& muon1,
					      const pat::Muon& muon2)
{
  std::vector<const reco::Track*> trks;
  std::vector<float> masses;
  trks.push_back( muon1.innerTrack().get() );
  masses.push_back(MuonMass_);
  trks.push_back( muon2.innerTrack().get() );
  masses.push_back(MuonMass_);
  return vertexWithKalmanFitter(trks,masses);
}


KinematicFitResult 
BxToMuMuProducer::vertexWithKinematicFitter(std::vector<const reco::Track*> trks,
					    std::vector<float> masses)
{
  // https://twiki.cern.ch/twiki/bin/view/CMSPublic/SWGuideKinematicVertexFit
  if ( trks.size() != masses.size() ) 
    throw cms::Exception("Error") << "number of tracks and number of masses should match";

  std::vector<reco::TransientTrack> transTrks;

  KinematicParticleFactoryFromTransientTrack factory;
  KinematicParticleVertexFitter fitter;
    
  std::vector<RefCountedKinematicParticle> particles;

  double chi = 0.;
  double ndf = 0.;
  float muonMassErr(MuonMassErr_);
  for (unsigned int i=0; i<trks.size(); ++i){
    transTrks.push_back((*theTTBuilder_).build(trks[i]));
    particles.push_back(factory.particle(transTrks.back(),masses[i],chi,ndf,muonMassErr));
  }

  RefCountedKinematicTree vertexFitTree = fitter.fit(particles);
  KinematicFitResult result;
    
  if ( !vertexFitTree->isValid()) return result;
  
  result.treeIsValid = true;

  vertexFitTree->movePointerToTheTop();
  result.refitVertex = vertexFitTree->currentDecayVertex();
  result.refitMother = vertexFitTree->currentParticle();
  result.refitTree   = vertexFitTree;
  if ( !result.refitVertex->vertexIsValid()) return result;

  result.vertexIsValid = true;

  // extract the re-fitted tracks
  vertexFitTree->movePointerToTheTop();
  
  if ( vertexFitTree->movePointerToTheFirstChild() ){
    do {
      result.refitDaughters.push_back(vertexFitTree->currentParticle());
    } while (vertexFitTree->movePointerToTheNextChild());
  }
  return result;
}

KinematicFitResult
BxToMuMuProducer::vertexMuonsWithKinematicFitter(const pat::Muon& muon1,
						 const pat::Muon& muon2)
{
  std::vector<const reco::Track*> trks;
  std::vector<float> masses;
  trks.push_back( muon1.innerTrack().get() );
  masses.push_back(MuonMass_);
  trks.push_back( muon2.innerTrack().get() );
  masses.push_back(MuonMass_);
  return vertexWithKinematicFitter(trks,masses);
}

KinematicFitResult
BxToMuMuProducer::vertexWithKinematicFitter(const pat::Muon& muon1,
					    const pat::Muon& muon2,
					    const pat::PackedCandidate& pion)
{
  std::vector<const reco::Track*> trks;
  std::vector<float> masses;
  trks.push_back( muon1.innerTrack().get() );
  masses.push_back(MuonMass_);
  trks.push_back( muon2.innerTrack().get() );
  masses.push_back(MuonMass_);
  trks.push_back( pion.bestTrack() );
  masses.push_back(pionMass_);
  return vertexWithKinematicFitter(trks,masses);
}

KinematicFitResult
BxToMuMuProducer::fitBToKJPsiMuMu( RefCountedKinematicParticle refitMuMu,
				   const pat::PackedCandidate &kaon,
				   bool applyJpsiMassConstraint)
{
  const reco::TransientTrack mmTT = refitMuMu->refittedTransientTrack();
  const reco::TransientTrack kaonTT = theTTBuilder_->build(kaon.bestTrack());

  KinematicParticleFactoryFromTransientTrack partFactory;
  KinematicParticleVertexFitter fitter;

  std::vector<RefCountedKinematicParticle> BToKMuMuParticles;
  double chi = 0.;
  double ndf = 0.;

  float MuMu_mass = refitMuMu->currentState().mass();
  float MuMu_mass_err = sqrt(refitMuMu->currentState().kinematicParametersError().matrix()(6,6));

  if ( applyJpsiMassConstraint ){
    MuMu_mass = JPsiMass_;
    MuMu_mass_err = JPsiMassErr_;
  }

  BToKMuMuParticles.push_back(partFactory.particle(mmTT,MuMu_mass,chi,ndf,MuMu_mass_err));
  float kaonMassErr(KaonMassErr_);
  BToKMuMuParticles.push_back(partFactory.particle(kaonTT,KaonMass_,chi,ndf,kaonMassErr));

  RefCountedKinematicTree vertexFitTree = fitter.fit(BToKMuMuParticles);
  KinematicFitResult result; 

  if ( !vertexFitTree->isValid()) return result;

  result.treeIsValid = true;

  vertexFitTree->movePointerToTheTop();
  result.refitVertex = vertexFitTree->currentDecayVertex();
  result.refitMother = vertexFitTree->currentParticle();
  result.refitTree   = vertexFitTree;

  if ( !result.refitVertex->vertexIsValid()) return result;

  result.vertexIsValid = true;

  // extract the re-fitted tracks
  vertexFitTree->movePointerToTheTop();

  if ( vertexFitTree->movePointerToTheFirstChild() ){
    do {
      result.refitDaughters.push_back(vertexFitTree->currentParticle());
    } while (vertexFitTree->movePointerToTheNextChild());
  }
  return result;
}

KinematicFitResult
BxToMuMuProducer::fitBToKJPsiMuMuNew( RefCountedKinematicTree jpsiTree,
				      const pat::PackedCandidate& kaon,
				      bool applyJpsiMassConstraint)
{
  KinematicFitResult result; 
  if ( !jpsiTree->isValid()) return result;

  KinematicConstraint* jpsi_mc(0);
  if (applyJpsiMassConstraint){
    ParticleMass jpsi = JPsiMass_;
    // jpsi mass constraint fit
    KinematicParticleFitter csFitter;
    float jp_m_sigma = JPsiMassErr_;
    // FIXME: memory leak
    jpsi_mc = new MassKinematicConstraint(jpsi, jp_m_sigma);
    jpsiTree = csFitter.fit(jpsi_mc, jpsiTree);
  }

  const reco::TransientTrack kaonTT = theTTBuilder_->build(kaon.bestTrack());

  KinematicParticleFactoryFromTransientTrack partFactory;
  KinematicParticleVertexFitter fitter;

  std::vector<RefCountedKinematicParticle> BToKMuMuParticles;
  double chi = 0.;
  double ndf = 0.;

  jpsiTree->movePointerToTheTop();
  BToKMuMuParticles.push_back(jpsiTree->currentParticle());
  float kaonMassErr(KaonMassErr_);
  BToKMuMuParticles.push_back(partFactory.particle(kaonTT,KaonMass_,chi,ndf,kaonMassErr));

  RefCountedKinematicTree vertexFitTree = fitter.fit(BToKMuMuParticles);

  if ( !vertexFitTree->isValid()) return result;

  result.treeIsValid = true;

  vertexFitTree->movePointerToTheTop();
  result.refitVertex = vertexFitTree->currentDecayVertex();
  result.refitMother = vertexFitTree->currentParticle();
  result.refitTree   = vertexFitTree;

  if ( !result.refitVertex->vertexIsValid()) return result;

  result.vertexIsValid = true;

  // extract the re-fitted tracks
  vertexFitTree->movePointerToTheTop();

  if ( vertexFitTree->movePointerToTheFirstChild() ){
    do {
      result.refitDaughters.push_back(vertexFitTree->currentParticle());
    } while (vertexFitTree->movePointerToTheNextChild());
  }
  return result;
}

KinematicFitResult
BxToMuMuProducer::fitBToKKMuMu( RefCountedKinematicTree jpsiTree,
				const pat::PackedCandidate& kaon1,
				const pat::PackedCandidate& kaon2,
				bool applyJpsiMassConstraint)
{
  KinematicFitResult result; 
  if ( !jpsiTree->isValid()) return result;

  KinematicConstraint* jpsi_mc(0);
  if (applyJpsiMassConstraint){
    ParticleMass jpsi = JPsiMass_;
    // jpsi mass constraint fit
    KinematicParticleFitter csFitter;
    float jp_m_sigma = JPsiMassErr_;
    // FIXME: memory leak
    jpsi_mc = new MassKinematicConstraint(jpsi, jp_m_sigma);
    jpsiTree = csFitter.fit(jpsi_mc, jpsiTree);
  }

  const reco::TransientTrack kaonTT1 = theTTBuilder_->build(kaon1.bestTrack());
  const reco::TransientTrack kaonTT2 = theTTBuilder_->build(kaon2.bestTrack());

  KinematicParticleFactoryFromTransientTrack partFactory;
  KinematicParticleVertexFitter fitter;

  std::vector<RefCountedKinematicParticle> BToKKMuMuParticles;
  double chi = 0.;
  double ndf = 0.;

  jpsiTree->movePointerToTheTop();
  float kaonMassErr(KaonMassErr_);
  BToKKMuMuParticles.push_back(jpsiTree->currentParticle());
  BToKKMuMuParticles.push_back(partFactory.particle(kaonTT1,KaonMass_,chi,ndf,kaonMassErr));
  BToKKMuMuParticles.push_back(partFactory.particle(kaonTT2,KaonMass_,chi,ndf,kaonMassErr));

  RefCountedKinematicTree vertexFitTree = fitter.fit(BToKKMuMuParticles);

  if ( !vertexFitTree->isValid()) return result;

  result.treeIsValid = true;

  vertexFitTree->movePointerToTheTop();
  result.refitVertex = vertexFitTree->currentDecayVertex();
  result.refitMother = vertexFitTree->currentParticle();
  result.refitTree   = vertexFitTree;

  if ( !result.refitVertex->vertexIsValid()) return result;

  result.vertexIsValid = true;

  // extract the re-fitted tracks
  vertexFitTree->movePointerToTheTop();

  if ( vertexFitTree->movePointerToTheFirstChild() ){
    do {
      result.refitDaughters.push_back(vertexFitTree->currentParticle());
    } while (vertexFitTree->movePointerToTheNextChild());
  }
  return result;
}

KinematicFitResult
BxToMuMuProducer::refitWithPointingConstraint( RefCountedKinematicTree tree,
					       const reco::Vertex& primaryVertex)
{
  KinematicFitResult result; 
  if ( !tree->isValid()) return result;

  GlobalPoint pv(primaryVertex.position().x(), 
		 primaryVertex.position().y(), 
		 primaryVertex.position().z());
  // FIXIT: potential memory leak
  PointingKinematicConstraint* pointing_constraint = new PointingKinematicConstraint(pv);

  KinematicParticleFitter fitter;

  tree->movePointerToTheTop(); // not sure it's needed

  RefCountedKinematicTree refittedTree = fitter.fit(pointing_constraint,tree);

  if ( !refittedTree->isValid()) return result;

  result.treeIsValid = true;

  refittedTree->movePointerToTheTop();
  result.refitVertex = refittedTree->currentDecayVertex();
  result.refitMother = refittedTree->currentParticle();
  result.refitTree   = refittedTree;

  if ( !result.refitVertex->vertexIsValid()) return result;

  result.vertexIsValid = true;

  // extract the re-fitted tracks
  refittedTree->movePointerToTheTop();

  if ( refittedTree->movePointerToTheFirstChild() ){
    do {
      result.refitDaughters.push_back(refittedTree->currentParticle());
    } while (refittedTree->movePointerToTheNextChild());
  }
  return result;
}

pair<double,double> BxToMuMuProducer::computeDCA(const pat::PackedCandidate &kaon,
                                                 reco::BeamSpot beamSpot){

  const reco::TransientTrack trackTT((*(kaon.bestTrack())), &(*bFieldHandle_));

  TrajectoryStateClosestToPoint theDCAXBS = trackTT.trajectoryStateClosestToPoint( GlobalPoint(beamSpot.position().x(),beamSpot.position().y(),beamSpot.position().z()) );  
  
  double DCABS = theDCAXBS.perigeeParameters().transverseImpactParameter();
  double DCABSErr = theDCAXBS.perigeeError().transverseImpactParameterError();
    
  pair<double,double> DCA = make_pair(DCABS,DCABSErr);
    
  return DCA;
}
namespace{

  bool dr_match(const LorentzVector& reco , const LorentzVector& gen){
    if (fabs(reco.pt()-gen.pt())/gen.pt()<0.1 and deltaR(reco,gen)<0.02)
      return true;
    return false;
  }

  std::vector<unsigned int> 
    get_depth_from_permutation(const std::vector<unsigned int>& elements){
    std::vector<unsigned int> result;
    unsigned int counter(0);
    for (auto element: elements){
      if (element==0){
	counter++;
      } else {
	result.push_back(counter);
	counter = 0;
      }
    }
    result.push_back(counter);
    return result;
  }

  bool is_acceptable(const reco::Candidate* cand){
    if ( not cand) return false; 
    // skip quarks
    if ( abs(cand->pdgId())<10 ) return false;
    // skip protons
    if ( abs(cand->pdgId())==2212 ) return false;
    // skip gluons
    if ( abs(cand->pdgId())==21 ) return false;
    return true;
  }

  // depth 0 - first mother

  const reco::Candidate* get_mother(const reco::Candidate* cand, unsigned int depth){
    if (not cand) return 0;
    const reco::Candidate* mother = cand->mother();
    unsigned int i = 0;
    while ( is_acceptable(mother) and i<depth ){
      i++;
      mother = mother->mother();
    }
    if (is_acceptable(mother))
      return mother;
    else
      return 0;
  }

  const reco::Candidate* 
    find_common_ancestor(const std::vector<const reco::Candidate*>& particles, 
			 unsigned int max_depth=10){
    auto n = particles.size();
    for (unsigned int depth=0; depth<max_depth; ++depth){
      // make a list of elements (0) and separators (1) and
      // find all possible permutations of the elements
      std::vector<unsigned int> elements;
      for (unsigned int i=0; i<depth; ++i)
	elements.push_back(0);
      for (unsigned int i=0; i<n-1; ++i)
	elements.push_back(1);
      do {
	auto depth_vector = get_depth_from_permutation(elements);
	const reco::Candidate* common_mother(0);
	for (unsigned int i=0; i<n; ++i){
	  auto mother = get_mother(particles[i],depth_vector[i]);
	  if (not mother) {
	    common_mother = 0;
	    break;
	  }
	  if (not common_mother) common_mother = mother;
	  if (common_mother != mother) {
	    common_mother = 0;
	    break;
	  }	  
	}
	if (common_mother) return common_mother;
      } while(std::next_permutation(elements.begin(), elements.end()));
    }
    return 0;
  }

}

GenMatchInfo BxToMuMuProducer::getGenMatchInfo( const edm::View<reco::GenParticle>& genParticles,
						const pat::Muon& muon1,
						const pat::Muon& muon2,
						const pat::PackedCandidate* kaon1,
						const pat::PackedCandidate* kaon2 )
{
  auto result = GenMatchInfo();
  const reco::GenParticle* mc_mu1(0);
  const reco::GenParticle* mc_mu2(0);
  const reco::GenParticle* mc_kaon1(0);
  const reco::GenParticle* mc_kaon2(0);
  const reco::Candidate*   mm_mother(0);
  std::vector<const reco::Candidate*> daughters;
  if (muon1.genParticle()){
    // should we use sim instead for more information?
    mc_mu1 = muon1.genParticle();
    result.mu1_pdgId = muon1.genParticle()->pdgId();
    result.mu1_pt    = muon1.genParticle()->pt();
    if (muon1.genParticle()->mother()){
      result.mu1_motherPdgId = muon1.genParticle()->mother()->pdgId();
    }
    daughters.push_back(mc_mu1);
  }
  if (muon2.genParticle()){
    mc_mu2 = muon2.genParticle();
    result.mu2_pdgId = muon2.genParticle()->pdgId();
    result.mu2_pt    = muon2.genParticle()->pt();
    if (muon2.genParticle()->mother()){
      result.mu2_motherPdgId = muon2.genParticle()->mother()->pdgId();
    }
    daughters.push_back(mc_mu2);
  }
  if ( mc_mu1 and mc_mu2 ){
    if ( (mc_mu1->vertex()-mc_mu2->vertex()).r() < 1e-4)
      result.mm_vtx    = mc_mu1->vertex();
    if ( mc_mu1->mother() and mc_mu1->mother() == mc_mu2->mother() ){
      mm_mother = mc_mu1->mother();
      result.mm_mass      = mm_mother->mass();
      result.mm_pt        = mm_mother->pt();
      result.mm_pdgId     = mm_mother->pdgId();
      if (mm_mother->mother()) result.mm_motherPdgId = mm_mother->mother()->pdgId();
      // handle oscillation and radiation
      const reco::Candidate* primary = mm_mother;
      while (primary->mother() and abs(primary->pdgId())==abs(primary->mother()->pdgId()))
	primary = primary->mother();
      result.mm_prod_vtx  = primary->vertex();
    }
  }
  
  if (kaon1){
    for (auto const & genParticle: genParticles){
      if (dr_match(kaon1->p4(),genParticle.p4())){
	mc_kaon1 = &genParticle;
	daughters.push_back(mc_kaon1);
	result.kaon1_pdgId = genParticle.pdgId();
	result.kaon1_pt    = genParticle.pt();
	if (genParticle.mother()){
	  result.kaon1_motherPdgId = genParticle.mother()->pdgId();
	}
	break;
      }
    }
    if (daughters.size()==3){
      const auto* mother = find_common_ancestor(daughters);
      if (mother){
	result.kmm_pdgId    = mother->pdgId();
	result.kmm_mass     = mother->mass();
	result.kmm_pt       = mother->pt();
	result.kmm_prod_vtx = mother->vertex();
      }
    }
  }
  if (kaon2){
    for (auto const & genParticle: genParticles){
      if (dr_match(kaon2->p4(),genParticle.p4())){
	mc_kaon2 = &genParticle;
	daughters.push_back(mc_kaon2);
	result.kaon2_pdgId = genParticle.pdgId();
	result.kaon2_pt    = genParticle.pt();
	if (genParticle.mother()){
	  result.kaon2_motherPdgId = genParticle.mother()->pdgId();
	}
	break;
      }
    }
    if (daughters.size()==4){
      const auto* mother = find_common_ancestor(daughters);
      if (mother){
	result.kkmm_pdgId    = mother->pdgId();
	result.kkmm_mass     = mother->mass();
	result.kkmm_pt       = mother->pt();
	result.kkmm_prod_vtx = mother->vertex();
      }
    }
  }

  return result;
}

float BxToMuMuProducer::distanceOfClosestApproach( const reco::Track* track1,
						   const reco::Track* track2)
{
  TwoTrackMinimumDistance md;
  const reco::TransientTrack tt1 = theTTBuilder_->build(track1);
  const reco::TransientTrack tt2 = theTTBuilder_->build(track2);
  if ( not md.calculate( tt1.initialFreeState(), tt2.initialFreeState() ) ) return -1.0;
  return md.distance();
}

Measurement1D 
BxToMuMuProducer::distanceOfClosestApproach( const reco::Track* track,
					     RefCountedKinematicVertex vertex)
{
  if (not vertex->vertexIsValid()) return Measurement1D(-1.0,-1.0);
  VertexDistance3D distance3D;
  const reco::TransientTrack tt = theTTBuilder_->build(track);
  assert(impactPointExtrapolator_);
  auto tsos = impactPointExtrapolator_->extrapolate(tt.initialFreeState(), vertex->position());
  if ( not tsos.isValid()) return Measurement1D(-1.0,-1.0);
  Measurement1D doca = distance3D.distance(VertexState(tsos.globalPosition(), tsos.cartesianError().position()), vertex->vertexState());
  return doca;
}

Measurement1D 
BxToMuMuProducer::distanceOfClosestApproach( const reco::Track* track,
					     const reco::Vertex& vertex)
{
  VertexDistance3D distance3D;
  const reco::TransientTrack tt = theTTBuilder_->build(track);
  assert(impactPointExtrapolator_);
  auto tsos = impactPointExtrapolator_->extrapolate(tt.initialFreeState(), GlobalPoint(Basic3DVector<float>(vertex.position())));
  if ( not tsos.isValid()) return Measurement1D(-1.0,-1.0);
  Measurement1D doca = distance3D.distance(VertexState(tsos.globalPosition(), tsos.cartesianError().position()), vertex);
  return doca;
}

DisplacementInformationIn3D BxToMuMuProducer::compute3dDisplacement(const KinematicFitResult& fit,
								    const reco::VertexCollection& vertices,
								    bool closestIn3D)
{
  DisplacementInformationIn3D result;
  if (not fit.valid()) return result;

  // Potential issue: tracks used to build the candidate could 
  // also be used in the primary vertex fit. One can refit the vertices
  // excluding tracks from the cadndidate. It's not done at the moment 
  // due to non-trivial linkig between primary vertex and its tracks 
  // in MiniAOD. Also not all muons associated to a vertex are really 
  // used in the fit, so the potential bias most likely small.
  
  auto candTransientTrack = fit.refitMother->refittedTransientTrack();

  const reco::Vertex* bestVertex(0);
  int bestVertexIndex(-1);
  double minDistance(999.);

  for ( unsigned int i = 0; i<vertices.size(); ++i ){
    const auto & vertex = vertices.at(i);
    if (closestIn3D){
      auto impactParameter3D = IPTools::absoluteImpactParameter3D(candTransientTrack, vertex);
      if (impactParameter3D.first and impactParameter3D.second.value() < minDistance){
	minDistance = impactParameter3D.second.value();
	bestVertex = &vertex;
	bestVertexIndex = i;
      }
    } else{
      auto impactParameterZ  = IPTools::signedDecayLength3D(candTransientTrack, GlobalVector(0,0,1), vertex);
      double distance = fabs(impactParameterZ.second.value());
      if (impactParameterZ.first and distance < minDistance){
	minDistance = distance;
	bestVertex = &vertex;
	bestVertexIndex = i;
      }
    }
  }

  // find second best vertex
  const reco::Vertex* bestVertex2(0);
  int bestVertexIndex2(-1);
  double minDistance2(999.);
  for ( unsigned int i = 0; i<vertices.size(); ++i ){
    const auto & vertex = vertices.at(i);
    if (closestIn3D){
      auto impactParameter3D = IPTools::absoluteImpactParameter3D(candTransientTrack, vertex);
      if (impactParameter3D.first and impactParameter3D.second.value() < minDistance2 and impactParameter3D.second.value() > minDistance){
	minDistance2 = impactParameter3D.second.value();
	bestVertex2 = &vertex;
	bestVertexIndex2 = i;
      }
    } else{
      auto impactParameterZ  = IPTools::signedDecayLength3D(candTransientTrack, GlobalVector(0,0,1), vertex);
      double distance = fabs(impactParameterZ.second.value());
      if (impactParameterZ.first and distance < minDistance2 and distance > minDistance){
	minDistance2 = distance;
	bestVertex2 = &vertex;
	bestVertexIndex2 = i;
      }
    }
  }

  if (! bestVertex) return result;

  auto impactParameter3D = IPTools::absoluteImpactParameter3D(candTransientTrack, *bestVertex);
  auto impactParameterZ  = IPTools::signedDecayLength3D(candTransientTrack, GlobalVector(0,0,1), *bestVertex);
  result.pv = bestVertex;
  result.pvIndex = bestVertexIndex;
  if (impactParameterZ.first) {
    result.longitudinalImpactParameter    = impactParameterZ.second.value();
    result.longitudinalImpactParameterErr = impactParameterZ.second.error();
  }
  if (impactParameter3D.first) {
    result.distaceOfClosestApproach       = impactParameter3D.second.value();
    result.distaceOfClosestApproachErr    = impactParameter3D.second.error();
  }

  // compute decay length
  VertexDistance3D distance3D;
  auto dist = distance3D.distance(*bestVertex, fit.refitVertex->vertexState() );
  result.decayLength    = dist.value();
  result.decayLengthErr = dist.error();
  
  if (bestVertex2){
    auto impactParameter3D2 = IPTools::absoluteImpactParameter3D(candTransientTrack, *bestVertex2);
    auto impactParameterZ2  = IPTools::signedDecayLength3D(candTransientTrack, GlobalVector(0,0,1), *bestVertex2);
    result.pv2 = bestVertex2;
    result.pv2Index = bestVertexIndex2;
    if (impactParameterZ2.first) {
      result.longitudinalImpactParameter2    = impactParameterZ2.second.value();
      result.longitudinalImpactParameter2Err = impactParameterZ2.second.error();
    }
    if (impactParameter3D2.first) {
      result.distaceOfClosestApproach2       = impactParameter3D2.second.value();
      result.distaceOfClosestApproach2Err    = impactParameter3D2.second.error();
    }

    // compute decay length
    VertexDistance3D distance3D;
    auto dist = distance3D.distance(*bestVertex2, fit.refitVertex->vertexState() );
    result.decayLength2    = dist.value();
    result.decayLength2Err = dist.error();
  }

  // cosAlpha

  TVector3 plab(fit.refitMother->currentState().globalMomentum().x(),
		fit.refitMother->currentState().globalMomentum().y(),
		fit.refitMother->currentState().globalMomentum().z());
  TVector3 p1(bestVertex->x(), bestVertex->y(), bestVertex->z());
  TVector3 p2(fit.refitVertex->vertexState().position().x(), 
	      fit.refitVertex->vertexState().position().y(), 
	      fit.refitVertex->vertexState().position().z());
  TVector3 pDiff = p2-p1;
  result.cosAlpha  = plab.Dot(pDiff) / (plab.Mag() * pDiff.Mag());
    
  return result;
}


// GenInfoSummary HeavyFlavDileptonNtupleMakerMiniAOD::getGenInfoSummary(const edm::Event& iEvent){
//   GenInfoSummary summary;
  
//   edm::Handle<edm::View<reco::GenParticle> > pruned;
//   iEvent.getByToken(pruned_gen_token,pruned);

//   // Packed particles are all the status 1, so usable to remake jets
//   // The navigation from status 1 to pruned is possible (the other direction should be made by hand)
//   edm::Handle<edm::View<pat::PackedGenParticle> > packed;
//   iEvent.getByToken(packed_gen_token,packed);

//   //let's try to find all status1 originating directly from a B meson decay
//   for (auto const & bMeson: *pruned){
//     if(abs(bMeson.pdgId()) != 521 and abs(bMeson.pdgId()) != 511) continue;
//     std::vector<const reco::Candidate*> b_muons;
//     std::vector<const reco::Candidate*> b_hadrons;
//     std::vector<const reco::Candidate*> other_muons;
//     for (auto const& pa: *packed){ 
//       //get the pointer to the first survied ancestor of a given packed GenParticle in the prunedCollection
//       const reco::Candidate * mother = pa.mother(0) ;
//       // FIXME: may need to check particle status to avoid intermediate gen particles
//       if(mother != nullptr && isAncestor( &bMeson , mother)){
// 	if (abs(pa.pdgId())==13)
// 	  b_muons.push_back(&pa);
// 	if (abs(pa.pdgId())==211 or abs(pa.pdgId())==321)
// 	  b_hadrons.push_back(&pa);
//       }else{
// 	if (abs(pa.pdgId())==13)
// 	  other_muons.push_back(&pa);
//       }
//     }
//     if (b_muons.size()==2 && b_hadrons.size()>0){
//       summary.b = &bMeson;
//       summary.b_muons = b_muons;
//       summary.b_hadrons = b_hadrons;
//       std::sort(other_muons.begin(),other_muons.end(), [](const reco::Candidate* a,const reco::Candidate* b){
// 	  return a->pt() > b->pt();
// 	});
//       summary.other_muons = other_muons;
//     }
//   }

//   return summary;
// }

void BxToMuMuProducer::setupTmvaReader(TMVA::Reader& reader, std::string file){
  reader.AddVariable("fls3d",    & bdtData_.fls3d);
  reader.AddVariable("alpha",    & bdtData_.alpha);
  reader.AddVariable("pvips",    & bdtData_.pvips);
  reader.AddVariable("iso",      & bdtData_.iso);
  reader.AddVariable("chi2dof",  & bdtData_.chi2dof);
  reader.AddVariable("docatrk",  & bdtData_.docatrk);
  reader.AddVariable("closetrk", & bdtData_.closetrk);
  reader.AddVariable("m1iso",    & bdtData_.m1iso);
  reader.AddVariable("m2iso",    & bdtData_.m2iso);
  reader.AddVariable("eta",      & bdtData_.eta);
  reader.AddSpectator("m",       & bdtData_.m);
  reader.BookMVA("BDTG",file);
}

float
BxToMuMuProducer::computeAnalysisBDT(unsigned int event_idx)
{
  switch (event_idx){
  case 0:
    return bdtReader0_.EvaluateMVA("BDTG");
    break;
  case 1:
    return bdtReader1_.EvaluateMVA("BDTG");
    break;
  case 2:
    return bdtReader2_.EvaluateMVA("BDTG");
    break;
  default:
    throw cms::Exception("FatalError") << "event index must be in [0-3] range\n";
  }
}


DEFINE_FWK_MODULE(BxToMuMuProducer);

//  LocalWords:  vertices
