#include <config.h>

#ifdef HAVE_MPI
#include <mpi.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <S4.h>
#include <Rcpp.h>

namespace S4{
/*
class CMaterial(){
	Material *M;
	Simulation *S;
public:
	CMaterial(Simulation *S, const std::string &name, const Rcomplex &eps){
	}
	CMaterial(Simulation *S, const std::string &name, const Rcpp::ComplexMatrix &eps){
	}
	~CMaterial(){
	}
};

class CLayer(){
	Layer *M;
	Simulation *S;
public:
	CLayer(){
	}
	~CLayer(){
	}
};
*/
class CSimulation{
	Simulation *S;

	void HandleSolutionErrorCode(const char *fname, int code){
		static const char def[] = "An unknown error occurred";
		static const char* errstr[] = {
			def, /* 0 */
			"A memory allocation error occurred", /* 1 */
			def, /* 2 */
			def, /* 3 */
			def, /* 4 */
			def, /* 5 */
			def, /* 6 */
			def, /* 7 */
			def, /* 8 */
			"NumG was not set", /* 9 */
			"A layer copy referenced an unknown layer", /* 10 */
			"A layer copy referenced another layer copy", /* 11 */
			"A duplicate layer name was found", /* 12 */
			"Excitation layer name not found", /* 13 */
			"No layers exist in the structure", /* 14 */
			"A material name was not found", /* 15 */
			"Invalid patterning for 1D lattice", /* 16 */
			def
		};
		const char *str = def;
		if(0 < code && code <= 16){
			str = errstr[code];
			::Rf_error("%s: %s.", fname, str);
		}else{
			::Rf_error("%s: %s. Error code: %d", fname, str, code);
		}
	}

public:
	CSimulation(){
		S = (Simulation*)malloc(sizeof(Simulation));
		Simulation_Init(S);

	}
	CSimulation(double period, int NumG){
		S = (Simulation*)malloc(sizeof(Simulation));
		Simulation_Init(S);
		S->Lr[0] = period;
		S->Lr[1] = 0;
		S->Lr[2] = 0;
		S->Lr[3] = 0;
		Simulation_MakeReciprocalLattice(S);
		Simulation_SetNumG(S, NumG);
	}
	CSimulation(Rcpp::NumericMatrix L, int NumG){
		S = (Simulation*)malloc(sizeof(Simulation));
		Simulation_Init(S);
		S->Lr[0] = L(0,0);
		S->Lr[1] = L(1,0);
		S->Lr[2] = L(0,1);
		S->Lr[3] = L(1,1);

		int i = Simulation_MakeReciprocalLattice(S);
		if(0 != i){
			switch(i){
			case 1: // degenerate
				::Rf_error("Lattice is degenerate");
				break;
			case 2: //
				::Rf_error("Lattice vectors are both zero");
				break;
			default:
				break;
			}
		}
		Simulation_SetNumG(S, NumG);
	}
	static bool MatrixConstructor(SEXP *s, int n){
		if(2 != n){ return false; }
		try {
			Rcpp::NumericMatrix L(Rcpp::as<Rcpp::NumericMatrix>(s[0]));
			int ng = Rcpp::as<int>(s[1]);
			if(L.nrow() != 2 || L.ncol() != 2){ return false; }
			if(ng < 1){ return false; }
		}catch(std::exception &ex){ return false; }
		return true;
	}
	static bool ScalarConstructor(SEXP *s, int n){
		if(2 != n){ return false; }
		try {
			double L = Rcpp::as<double>(s[0]);
			int ng = Rcpp::as<int>(s[1]);
			if(L <= 0 || ng < 1){ return false; }
		}catch(std::exception &ex){ return false; }
		return true;
	}
	~CSimulation(){
		Simulation_Destroy(S);
		free(S);
	}

	Rcpp::XPtr<Material> SetMaterial(const std::string &name, const Rcomplex &eps){
		Material *M = Simulation_GetMaterialByName(S, name.c_str(), NULL);
		bool existed = true;
		if(NULL == M){
			M = Simulation_AddMaterial(S);
			if(NULL == M){
				::Rf_error("SetMaterial: There was a problem allocating the material named '%s'.", name.c_str());
				return Rcpp::XPtr<Material>((Material*)NULL);
			}
			existed = false;
		}
		if(!existed){
			const double deps[2] = { eps.r, eps.i };
			Material_Init(M, name.c_str(), deps);
		}else{
			M->eps.s[0] = eps.r;
			M->eps.s[1] = eps.i;
		}
		return Rcpp::XPtr<Material>(M);
	}
	static bool ScalarMaterial(SEXP *s, int n){
		if(2 != n){ return false; }
		try {
			Rcomplex eps(Rcpp::as<Rcomplex>(s[1]));
			(void)eps; /* suppress unused variable warning */
		}catch(std::exception &ex){ return false; }
		return true;
	}
	Rcpp::XPtr<Material> SetTensorMaterial(const std::string &name, const Rcpp::ComplexMatrix &eps){
		Material *M = Simulation_GetMaterialByName(S, name.c_str(), NULL);
		bool existed = true;
		if(NULL == M){
			M = Simulation_AddMaterial(S);
			if(NULL == M){
				::Rf_error("SetMaterial: There was a problem allocating the material named '%s'.", name.c_str());
				return Rcpp::XPtr<Material>((Material*)NULL);
			}
			existed = false;
		}
		double deps[18] = {
			eps(0,0).r, eps(0,0).i,
			eps(0,1).r, eps(0,1).i,
			eps(0,2).r, eps(0,2).i,
			eps(1,0).r, eps(1,0).i,
			eps(1,1).r, eps(1,1).i,
			eps(1,2).r, eps(1,2).i,
			eps(2,0).r, eps(2,0).i,
			eps(2,1).r, eps(2,1).i,
			eps(2,2).r, eps(2,2).i
		};
		/* [ a b c ]    [ a b   ]
		 * [ d e f ] -> [ d e   ]
		 * [ g h i ]    [     i ]
		 */
		if(!existed){
			Material_InitTensor(M, name.c_str(), deps);
		}else{
			deps[4] = deps[6]; deps[5] = deps[7];
			deps[6] = deps[8]; deps[7] = deps[9];
			deps[8] = deps[16]; deps[9] = deps[17];
			for(int i = 0; i < 10; ++i){
				M->eps.abcde[i] = deps[i];
			}
		}
		return Rcpp::XPtr<Material>(M);
	}
	static bool TensorMaterial(SEXP *s, int n){
		if(2 != n){ return false; }
		try {
			Rcpp::ComplexMatrix eps(Rcpp::as<Rcpp::ComplexMatrix>(s[1]));
			if(3 != eps.nrow() || 3 != eps.ncol()){ return false; }
		}catch(std::exception &ex){ return false; }
		return true;
	}
	Rcpp::XPtr<Layer> AddLayer(const std::string &name, const double thickness, Rcpp::XPtr<Material> mat){
		Layer *layer = Simulation_AddLayer(S);
		if(NULL == layer){
			::Rf_error("AddLayer: There was a problem allocating the layer named '%s'.", name.c_str());
				return Rcpp::XPtr<Layer>((Layer*)NULL);
		}
		Material *M = mat.checked_get();
		Layer_Init(layer,
			name.c_str(),
			thickness,
			M->name,
			NULL);

		return Rcpp::XPtr<Layer>(layer);
	}
	Rcpp::XPtr<Layer> PatternCircle(Rcpp::XPtr<Layer> player, Rcpp::XPtr<Material> mat, const Rcpp::NumericVector &vcenter, const double &radius){
		int ret;
		Layer *layer = player.checked_get();

		if(NULL != layer->copy){
			::Rf_error("pattern_circle: Cannot pattern a layer copy.");
			return player;
		}

		int material_index;
		Material *M = mat.checked_get();
		M = Simulation_GetMaterialByName(S, M->name, &material_index);
		if(NULL == M){
			::Rf_error("pattern_circle: Material named '%s' not found.", M->name);
			return player;
		}
		double center[2] = { vcenter[0], vcenter[1] };
		ret = Simulation_AddLayerPatternCircle(S, layer, material_index, center, radius);
		if(0 != ret){
			::Rf_error("pattern_circle: There was a problem allocating the pattern.");
			return player;
		}
		return player;
	}

	void ExcitationPlanewave(const Rcpp::NumericVector &angles, const Rcomplex &s, const Rcomplex &p){
		int ret;
		double angle[2];
		double pol_s[2]; /* s polarization; E out of plane */
		double pol_p[2]; /* p polarization; E in plane */

		Simulation_DestroySolution(S);

		angle[0] = angles[0];
		angle[1] = angles[1];

		pol_s[0] = hypot(s.r, s.i);
		pol_s[1] = atan2(s.i, s.r);
		pol_p[0] = hypot(p.r, p.i);
		pol_p[1] = atan2(p.i, p.r);

		ret = Simulation_MakeExcitationPlanewave(S, angle, pol_s, pol_p, 0);
		if(0 != ret){
			HandleSolutionErrorCode("excitation_planewave", ret);
		}
	}
	void SetFrequency(const Rcomplex &freq){
		Simulation_DestroySolution(S);

		S->omega[0] = 2*M_PI*freq.r;
		S->omega[1] = 2*M_PI*freq.i;
		if(S->omega[0] <= 0){
			::Rf_error("set_frequency: Frequency must be positive.");
			return;
		}
		if(S->omega[1] > 0){
			::Rf_error("set_frequency: Imaginary component of frequency must be negative.");
			return;
		}
	}
	Rcpp::NumericMatrix GetGList(){
		int *G;
		int n, ret;

		ret = Simulation_InitSolution(S);
		if(0 != ret){
			HandleSolutionErrorCode("get_G_list", ret);
			return Rcpp::NumericMatrix(0,0);
		}

		n = Simulation_GetNumG(S, &G);
		if(NULL == G){
			return Rcpp::NumericMatrix(0,0);
		}

		Rcpp::NumericMatrix Gmat(2, n);
		for(int i = 0; i < n; ++i){
			Gmat(0, i) = G[2*i+0];
			Gmat(1, i) = G[2*i+1];
		}
		return Gmat;
	}
	Rcpp::List GetPowerFlux(Rcpp::XPtr<Layer> player, const double &offset){
		double power[4];
		int ret;
		Layer *layer = player.checked_get();

		ret = Simulation_GetPoyntingFlux(S, layer, offset, power);

		if(0 != ret){
			HandleSolutionErrorCode("GetPoyntingFlux", ret);
		}
		Rcomplex forw; forw.r = power[0]; forw.i = power[2];
		Rcomplex back; back.r = power[1]; back.i = power[3];
		return Rcpp::List::create(
			Rcpp::Named("forward")  = forw,
			Rcpp::Named("backward") = back
		);
	}
	Rcpp::List GetAmplitudes(Rcpp::XPtr<Layer> player, const double &offset){
		double *amp;
		int *G;
		int n, i, ret;
		Layer *layer = player.checked_get();

		ret = Simulation_SolveLayer(S, layer);
		if(0 != ret){
			HandleSolutionErrorCode("get_amplitudes", ret);
			return Rcpp::List::create();
		}

		n = Simulation_GetNumG(S, &G);
		if(NULL == G){
			return Rcpp::List::create();
		}

		amp = (double*)malloc(sizeof(double)*8*n);
		Simulation_GetAmplitudes(S, layer, offset, amp, &amp[4*n]);

		Rcpp::ComplexVector hxf(n);
		Rcpp::ComplexVector hyf(n);
		Rcpp::ComplexVector hxb(n);
		Rcpp::ComplexVector hyb(n);

		for(i = 0; i < n; ++i){
			Rcomplex v;
			v.r = amp[4*n*0+2*n*0+2*i+0]; v.i = amp[4*n*0+2*n*0+2*i+1];
			hxf[i] = v;
			v.r = amp[4*n*0+2*n*1+2*i+0]; v.i = amp[4*n*0+2*n*1+2*i+1];
			hyf[i] = v;
			v.r = amp[4*n*1+2*n*0+2*i+0]; v.i = amp[4*n*1+2*n*0+2*i+1];
			hxb[i] = v;
			v.r = amp[4*n*1+2*n*1+2*i+0]; v.i = amp[4*n*1+2*n*1+2*i+1];
			hyb[i] = v;
		}

		free(amp);
		return Rcpp::List::create(
			Rcpp::Named("forward")  = Rcpp::List::create(
				Rcpp::Named("hx") = hxf,
				Rcpp::Named("hy") = hyf
			),
			Rcpp::Named("backward") = Rcpp::List::create(
				Rcpp::Named("hx") = hxb,
				Rcpp::Named("hy") = hyb
			)
		);
	}
};

} // namespace S4

RCPP_MODULE(S4RCWA){
	using namespace Rcpp;

	class_< ::S4::CSimulation>("Simulation")

	.constructor<double,int>("Creates a new Simulation object with a 1D lattice", &::S4::CSimulation::ScalarConstructor)
	.constructor<Rcpp::NumericMatrix,int>("Creates a new Simulation object with a 2D lattice", &::S4::CSimulation::MatrixConstructor)

	.method("set_material", &::S4::CSimulation::SetMaterial, "Set a scalar epsilon material", &::S4::CSimulation::ScalarMaterial)
	.method("set_material", &::S4::CSimulation::SetTensorMaterial, "Set a tensor epsilon material", &::S4::CSimulation::TensorMaterial)
	.method("add_layer", &::S4::CSimulation::AddLayer, "Add an unpatterned layer")
	.method("pattern_circle", &::S4::CSimulation::PatternCircle, "Pattern a layer with a circle")
	.method("excitation_planewave", &::S4::CSimulation::ExcitationPlanewave, "Set a planewave excitation")
	.method("set_frequency", &::S4::CSimulation::SetFrequency, "Sets the operating frequency")
	.method("get_G_list", &::S4::CSimulation::GetGList, "Gets the list of G vectors (integer coordinates in reciprocal lattice basis")
	.method("get_power_flux", &::S4::CSimulation::GetPowerFlux, "Gets net power flux in a layer at a given offset")
	.method("get_amplitudes", &::S4::CSimulation::GetAmplitudes, "Gets amplitudes of all modal bases in a given layer")
	;
}
