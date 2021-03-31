from PhysicsTools.NanoAOD.common_cff import *
import FWCore.ParameterSet.Config as cms
import re

# NOTE: 
#    All instances of FlatTableProducers must end with Table in their
#    names so that their product match the keep patterns in the default
#    event content. Otherwise you need to modify outputCommands in
#    NanoAODEDMEventContent or provide a custom event content to the
#    output module

# can use cms.PSet.clone() method instead
def merge_psets(*argv):
    result = cms.PSet()
    for pset in argv:
        if isinstance(pset, cms._Parameterizable):
            for name in pset.parameters_().keys():
                value = getattr(pset,name)
                type = value.pythonTypeName()
                setattr(result,name,value)
    return result

BmmMuonId = cms.EDProducer("BmmMuonIdProducer",
    muonCollection = cms.InputTag("linkedObjects","muons"),
    prunedGenParticleCollection = cms.InputTag("prunedGenParticles"),
    packedGenParticleCollection = cms.InputTag("packedGenParticles"),
    isMC = cms.bool(False)
)

BmmMuonIdMc = BmmMuonId.clone( isMC = cms.bool(True) ) 

BmmMuonIdVariables = cms.PSet(
    trkKink             = Var("userFloat('trkKink')",      float, doc = "Inner track kink chi2"),
    glbTrackProbability = Var("userFloat('trkKink')",      float, doc = "Log probability of the global fit"),
    match1_dX           = Var("userFloat('match1_dX')",    float, doc = "Station 1 local segment-track dX"),
    match1_pullX        = Var("userFloat('match1_pullX')", float, doc = "Station 1 local segment-track dX/dErr"),
    match1_dY           = Var("userFloat('match1_dY')",    float, doc = "Station 1 local segment-track dY"),
    match1_pullY        = Var("userFloat('match1_pullY')", float, doc = "Station 1 local segment-track dY/dErr"),
    match2_dX           = Var("userFloat('match2_dX')",    float, doc = "Station 2 local segment-track dX"),
    match2_pullX        = Var("userFloat('match2_pullX')", float, doc = "Station 2 local segment-track dX/dErr"),
    match2_dY           = Var("userFloat('match2_dY')",    float, doc = "Station 2 local segment-track dY"),
    match2_pullY        = Var("userFloat('match2_pullY')", float, doc = "Station 2 local segment-track dY/dErr"),
)

BmmMuonIdMcVariables = merge_psets(
    BmmMuonIdVariables,
    cms.PSet(
        simType             = Var("userInt('simType')",        int, doc = "reco::MuonSimType"),
        simExtType          = Var("userInt('simExtType')",     int, doc = "reco::ExtendedMuonSimType"),
        simPdgId            = Var("userInt('simPdgId')",       int, doc = "SIM particle pdgId"),
        simMotherPdgId      = Var("userInt('simMotherPdgId')", int, doc = "SIM particle mother pdgId"),
        simProdRho          = Var("userFloat('simProdRho')", float, doc = "SIM particle production vertex"),
        simProdZ            = Var("userFloat('simProdZ')",   float, doc = "SIM particle production vertex"),
        ),
)

BmmMuonIdTable=cms.EDProducer("SimpleCompositeCandidateFlatTableProducer", 
    src=cms.InputTag("BmmMuonId","muons"),
    cut=cms.string(""),
    name=cms.string("MuonId"),
    doc=cms.string("Muon Id Variables"),
    singleton=cms.bool(False),
    extension=cms.bool(False),
    variables = BmmMuonIdVariables
)
BmmMuonIdMcTable=cms.EDProducer("SimpleCompositeCandidateFlatTableProducer", 
    src=cms.InputTag("BmmMuonIdMc","muons"),
    cut=cms.string(""),
    name=cms.string("MuonId"),
    doc=cms.string("Muon Id Variables"),
    singleton=cms.bool(False),
    extension=cms.bool(False),
    variables = BmmMuonIdMcVariables
)

BmmMuonIdSequence   = cms.Sequence(BmmMuonId)
BmmMuonIdMcSequence = cms.Sequence(BmmMuonIdMc)
BmmMuonIdTables     = cms.Sequence(BmmMuonIdTable)
BmmMuonIdMcTables   = cms.Sequence(BmmMuonIdMcTable)
