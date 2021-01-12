/*
  Copyright (C) 2019 - 2020 by the authors of the ASPECT code.

  This file is part of ASPECT.

  ASPECT is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  ASPECT is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with ASPECT; see the file LICENSE.  If not see
  <http://www.gnu.org/licenses/>.
*/

#include <aspect/material_model/rheology/friction_options.h>

#include <deal.II/base/signaling_nan.h>
#include <deal.II/base/parameter_handler.h>
#include <aspect/utilities.h>
#include <aspect/geometry_model/interface.h>

#include <deal.II/fe/fe_values.h>
#include <deal.II/base/quadrature_lib.h>

namespace aspect
{
  namespace MaterialModel
  {
    namespace Rheology
    {
      template <int dim>
      double
      FrictionOptions<dim>::
      compute_dependent_friction_angle(const double current_edot_ii,
                                       const unsigned int j,  // j is from a for-loop over volume fractions
                                       const std::vector<double> &composition,
                                       typename DoFHandler<dim>::active_cell_iterator current_cell,
                                       double current_friction,
                                       const Point<dim> &position) const
      {

        switch (friction_dependence_mechanism)
          {
            case independent:
            {
              break;
            }
            case dynamic_friction:
            {
              // The dynamic characteristic strain rate is used to see what value between dynamic and static angle of internal friction should be used.
              // This is done as in the material model dynamic friction which is based on Equation (13) in van Dinther et al., (2013, JGR). Although here
              // the dynamic friction coefficient is directly specified. Furthermore a smoothness exponent X is added, which determines whether the
              // friction vs strain rate curve is rather step-like or more gradual.
              // mu  = mu_d + (mu_s - mu_d) / ( (1 + strain_rate_dev_inv2/dynamic_characteristic_strain_rate)^X );
              // Angles of friction are used in radians within ASPECT. The coefficient of friction is the tangent of the internal angle of friction.
              const double mu = effective_friction_factor[j] * std::tan(dynamic_angles_of_internal_friction[j])
                                + (std::tan(current_friction) - std::tan(dynamic_angles_of_internal_friction[j]))
                                / (1. + std::pow((current_edot_ii / dynamic_characteristic_strain_rate),
                                                 dynamic_friction_smoothness_exponent));
              current_friction = std::atan (mu);
              break;
            }
            case rate_and_state_dependent_friction:
            {
              // Cellsize is needed for theta and the friction angle
              // For now, the used cells are non-deforming squares, so the edge length in the
              // x-direction is representative of the cell size.
              // TODO as the cell size is used to compute the slip velocity as cell_size * strain_rate,
              //  come up with a better representation of the slip length.
              double cellsize = 1.;
              if (current_cell.state() == IteratorState::valid)
                {
                  cellsize = current_cell->extent_in_direction(0);
                  // Calculate the state variable theta
                  // theta_old loads theta from previous time step
                  const unsigned int theta_position_tmp = this->introspection().compositional_index_for_name("theta");
                  const double theta_old = composition[theta_position_tmp];
                  // Equation (7) from Sobolev and Muldashev (2017)
                  const double theta = compute_theta(theta_old, current_edot_ii, cellsize);

                  // Get the values for a and b
                  const double rate_and_state_parameter_a = calculate_depth_dependent_a_and_b(position,j).first;
                  const double rate_and_state_parameter_b = calculate_depth_dependent_a_and_b(position,j).second;

                  // Calculate effective friction according to Equation (4) in Sobolev and Muldashev (2017):
                  // mu = mu_st + a ln(V/V_st) + b ln((theta Vst)/L)
                  // Effective friction is calculated by multiplying the friction coefficient with the
                  // effective_friction_factor to account for effects of pore fluid pressure:
                  // mu = mu(1-p_f/sigma_n) = mu*, with (1-p_f/sigma_n) = 0.03 for subduction zones.
                  // Their equation is for friction coefficient, while ASPECT takes friction angle in radians,
                  // so conversion with tan/atan().
                  //const double current_friction_old = current_friction; // also only for chasing negative friction
                  current_friction = atan(effective_friction_factor[j]
                                          * (tan(current_friction)
                                             + rate_and_state_parameter_a
                                             * log((current_edot_ii * cellsize ) / quasi_static_strain_rate)
                                             + rate_and_state_parameter_b
                                             * log((theta * quasi_static_strain_rate ) / critical_slip_distance)));

                  /*// chasing the origin of negative friction angles
                  if (theta <= 0)
                    {
                      std::cout << "Theta is zero/negative: " << theta << " at time " << this->get_time() << std::endl;
                      std::cout << "Previous theta was: " << theta_old << std::endl;
                      std::cout << "current edot ii * cellsize is " << current_edot_ii *cellsize << std::endl;
                      std::cout << "current edot ii is " << current_edot_ii<< std::endl;
                      std::cout << "the friction coeff at this time is: " << tan(current_friction) << " and the friction angle in RAD is " << current_friction << std::endl;
                      std::cout << "the friction angle in degree is: " << current_friction*180/3.1516 << std::endl;
                      //AssertThrow(false, ExcMessage("Theta negative."));
                    }
                  if (current_friction <= 0)
                    {
                      std::cout << "current_friction is zero/negative!"<<std::endl;
                      std::cout << "Theta is " << theta << " at time " << this->get_time() << std::endl;
                      std::cout << "Previous theta was: " << theta_old << std::endl;
                      std::cout << "current edot ii * cellsize is " << current_edot_ii *cellsize << std::endl;
                      std::cout << "current edot ii is " << current_edot_ii<< std::endl;
                      std::cout << "the friction coeff at this time is: " << tan(current_friction) << " and the friction angle in RAD is " << current_friction << std::endl;
                      std::cout << "the friction angle in degree is: " << current_friction*180/3.1516 << std::endl;
                      std::cout << "the effective friction factor is: " << effective_friction_factor[j] << std::endl;
                      std::cout << " the current_friction before the RSF is [RAD] " << current_friction_old << std::endl;
                      std::cout << "a is: "<<rate_and_state_parameter_a<< " and b is: "<< rate_and_state_parameter_b << std::endl;
                    }*/
                }
              break;
            }
            case rate_and_state_dependent_friction_plus_linear_slip_weakening:
            {
              break;
            }
            case slip_rate_dependent_rate_and_state_dependent_friction:
            {
              // Cellsize is needed for theta and the friction angle
              // For now, the used cells are non-deforming squares, so the edge length in the
              // x-direction is representative of the cell size.
              // TODO as the cell size is used to compute the slip velocity as cell_size * strain_rate,
              //  come up with a better representation of the slip length.
              double cellsize = 1.;
              if (current_cell.state() == IteratorState::valid)
                {
                  cellsize = current_cell->extent_in_direction(0);
                  // Calculate the state variable theta
                  // theta_old loads theta from previous time step
                  const unsigned int theta_position_tmp = this->introspection().compositional_index_for_name("theta");
                  const double theta_old = composition[theta_position_tmp];
                  // Equation (7) from Sobolev and Muldashev (2017)
                  const double theta = compute_theta(theta_old, current_edot_ii, cellsize);

                  // Get the values for a and b
                  double rate_and_state_parameter_a = calculate_depth_dependent_a_and_b(position,j).first;
                  double rate_and_state_parameter_b = calculate_depth_dependent_a_and_b(position,j).second;

                  rate_and_state_parameter_a = rate_and_state_parameter_a + slope_s_for_a
                                               * log10((ref_v_for_a + current_edot_ii * cellsize)/ref_v_for_a);

                  const double dependent_critical_slip_distance = critical_slip_distance + slope_s_for_L
                                                                  * log10((ref_v_for_L + current_edot_ii * cellsize)/ref_v_for_L);

                  // Calculate effective friction according to Equation (4) in Sobolev and Muldashev (2017):
                  // mu = mu_st + a ln(V/V_st) + b ln((theta Vst)/L)
                  // Effective friction is calculated by multiplying the friction coefficient with the
                  // effective_friction_factor to account for effects of pore fluid pressure:
                  // mu = mu(1-p_f/sigma_n) = mu*, with (1-p_f/sigma_n) = 0.03 for subduction zones.
                  // Their equation is for friction coefficient, while ASPECT takes friction angle in radians,
                  // so conversion with tan/atan().
                  current_friction = atan(effective_friction_factor[j]
                                          * (tan(current_friction)
                                             + rate_and_state_parameter_a
                                             * log((current_edot_ii * cellsize ) / quasi_static_strain_rate)
                                             + rate_and_state_parameter_b
                                             * log((theta * quasi_static_strain_rate ) / dependent_critical_slip_distance)));
                }
              break;
            }
            case regularized_rate_and_state_friction:
            {
              // TODO as the cell size is used to compute the slip velocity as cell_size * strain_rate,
              //  come up with a better representation of the slip length.
              double cellsize = 1.;
              if (current_cell.state() == IteratorState::valid)
                {
                  cellsize = current_cell->extent_in_direction(0);
                  // Calculate the state variable theta
                  // theta_old loads theta from previous time step
                  const unsigned int theta_position_tmp = this->introspection().compositional_index_for_name("theta");
                  const double theta_old = composition[theta_position_tmp];
                  // Equation (7) from Sobolev and Muldashev (2017)
                  const double theta = compute_theta(theta_old, current_edot_ii, cellsize);

                  // Get the values for a and b
                  const double rate_and_state_parameter_a = calculate_depth_dependent_a_and_b(position,j).first;
                  const double rate_and_state_parameter_b = calculate_depth_dependent_a_and_b(position,j).second;

                  // Calculate regularized rate and state friction (e.g. Herrendörfer 2018) with
                  // mu = a sinh^{-1}[V/(2V_0)exp((mu_0 + b ln(V_0 theta/L))/a)]
                  // Effective friction is calculated by multiplying the friction coefficient with the
                  // effective_friction_factor to account for effects of pore fluid pressure:
                  // mu = mu(1-p_f/sigma_n) = mu*, with (1-p_f/sigma_n) = 0.03 for subduction zones.
                  // Their equation is for friction coefficient, while ASPECT takes friction angle in radians,
                  // so conversion with tan/atan().
                  current_friction = atan(effective_friction_factor[j]
                                          * (rate_and_state_parameter_a
                                             * asinh((current_edot_ii * cellsize ) / (2 * quasi_static_strain_rate)
                                                     * exp((tan(current_friction)
                                                            + rate_and_state_parameter_b
                                                            * log((theta * quasi_static_strain_rate ) / critical_slip_distance))
                                                           / rate_and_state_parameter_a))));
                }
              break;
            }
          }
          // if friction negative, that does not make sense and will get rate and state friction 
          // into trouble, so return some very small value
        if (current_friction < 0)
          current_friction = 1e-30;
        return current_friction;
      }



      template <int dim>
      ComponentMask
      FrictionOptions<dim>::
      get_theta_composition_mask(ComponentMask composition_mask) const
      {
        // Store which components to exclude during the volume fraction computation.

        // This is the compositional field used for theta in rate-and-state friction
        const int theta_position_tmp = this->introspection().compositional_index_for_name("theta");
        composition_mask.set(theta_position_tmp,false);

        return composition_mask;
      }



      template <int dim>
      double
      FrictionOptions<dim>::
      compute_theta(const double theta_old,
                    const double current_edot_ii,
                    const double cellsize) const
      {
        // Equation (7) from Sobolev and Muldashev (2017):
        // theta_{n+1} = L/V_{n+1} + (theta_n - L/V_{n+1})*exp(-(V_{n+1}dt)/L)
        // This is obtained from Equation (5): dtheta/dt = 1 - (theta V)/L
        // by integration using the assumption that velocities are constant at any time step.
        double current_theta = critical_slip_distance / ( cellsize * current_edot_ii ) +
                               (theta_old - critical_slip_distance / ( cellsize * current_edot_ii))
                               * exp( - ((current_edot_ii * cellsize) * this->get_timestep()) / critical_slip_distance);

        // TODO: make dt the min theta?
        if (current_theta <= 0)
          current_theta = 1e-50;
        return current_theta;
      }



      template <int dim>
      void
      FrictionOptions<dim>::
      compute_theta_reaction_terms(const int q,
                                   const MaterialModel::MaterialModelInputs<dim> &in,
                                   const double min_strain_rate,
                                   const double ref_strain_rate,
                                   const bool use_elasticity,
                                   const bool use_reference_strainrate,
                                   const double &average_elastic_shear_moduli,
                                   const double dte,
                                   MaterialModel::MaterialModelOutputs<dim> &out) const
      {
        // Cellsize is needed for theta and the friction angle
        double cellsize = 1.;
        if (in.current_cell.state() == IteratorState::valid)
          {
            cellsize = in.current_cell->extent_in_direction(0);
          }

        if (this->simulator_is_past_initialization() && this->get_timestep_number() > 0
            && in.requests_property(MaterialProperties::reaction_terms)
            && in.current_cell.state() == IteratorState::valid)
          {
            // q is from a for-loop through n_evaluation_points
            const double current_edot_ii =
              MaterialUtilities::compute_current_edot_ii (in.composition[q], ref_strain_rate,
                                                          min_strain_rate, in.strain_rate[q],
                                                          average_elastic_shear_moduli, use_elasticity,
                                                          use_reference_strainrate, dte);
            //std::cout << "q: " << q << " j: "<< j << " in.composition[q][j] " << in.composition[q][j] << std::endl;
            //}
            const unsigned int theta_position_tmp = this->introspection().compositional_index_for_name("theta");
            const double theta_old = in.composition[q][theta_position_tmp];
            const double current_theta = compute_theta(theta_old, current_edot_ii, cellsize);
            const double theta_increment = current_theta - theta_old;
            /*if (current_theta <= 0)
              {
                std::cout << "Reaction terms!"<< std::endl << "Theta is zero/negative: " << current_theta << " at time " << this->get_time() << std::endl;
                std::cout << "Previous theta was: " << theta_old << std::endl;
                std::cout << "current edot ii * cellsize is " << current_edot_ii *cellsize << std::endl;
                std::cout << "current edot ii is " << current_edot_ii<< std::endl;
              }
            //std::cout << "Reaction terms!"<< std::endl << "Theta is: " << current_theta << " at time " << this->get_time() << std::endl;
            //std::cout << "Previous theta was: " << theta_old << std::endl;
            //   std::cout << "current edot ii * cellsize is " << current_edot_ii *cellsize << std::endl;
            //std::cout << "q: " << q << " j: "<< j << " in.composition[q][j] " << in.composition[q][j] << std::endl; */

            out.reaction_terms[q][theta_position_tmp] = theta_increment;
          }
      }



      template <int dim>
      std::pair<double,double>
      FrictionOptions<dim>::calculate_depth_dependent_a_and_b(const Point<dim> &position, const int j) const
      {
        Utilities::NaturalCoordinate<dim> point_a =
          this->get_geometry_model().cartesian_to_other_coordinates(position, coordinate_system_a);
        Utilities::NaturalCoordinate<dim> point_b =
          this->get_geometry_model().cartesian_to_other_coordinates(position, coordinate_system_b);

        const double rate_and_state_parameter_a =
          rate_and_state_parameter_a_function->value(Utilities::convert_array_to_point<dim>(point_a.get_coordinates()),j);
        const double rate_and_state_parameter_b =
          rate_and_state_parameter_b_function->value(Utilities::convert_array_to_point<dim>(point_b.get_coordinates()),j);
        if (rate_and_state_parameter_a <0)
          AssertThrow(false, ExcMessage("The rate-and-state parameter a must be >= 0."));
        if (rate_and_state_parameter_b <0)
          AssertThrow(false, ExcMessage("The rate-and-state parameter b must be >= 0."));

        // std::cout << " a is " << rate_and_state_parameter_a << " - and b is " << rate_and_state_parameter_b << std::endl;
        return std::pair<double,double>(rate_and_state_parameter_a,
                                        rate_and_state_parameter_b);
      }



      template <int dim>
      FrictionDependenceMechanism
      FrictionOptions<dim>::
      get_friction_dependence_mechanism() const
      {
        return friction_dependence_mechanism;
      }



      template <int dim>
      bool
      FrictionOptions<dim>::
      get_use_theta() const
      {
        bool use_theta = false;
        // TODO add the RSF options
        if (get_friction_dependence_mechanism() == rate_and_state_dependent_friction)
          use_theta = true;

        return use_theta;
      }



      template <int dim>
      bool
      FrictionOptions<dim>::
      get_use_radiation_damping() const
      {
        return use_radiation_damping;
      }



      template <int dim>
      void
      FrictionOptions<dim>::declare_parameters (ParameterHandler &prm)
      {
        prm.declare_entry ("Friction dependence mechanism", "none",
                           Patterns::Selection("none|dynamic friction|rate and state dependent friction|"
                                               "rate and state dependent friction plus linear slip weakening|"
                                               "slip rate dependent rate and state dependent friction|"
                                               "regularized rate and state friction"),
                           "Whether to apply a rate or rate and state dependence of the friction angle. This can "
                           "be used to obtain stick-slip motion to simulate earthquake-like behaviour, "
                           "where short periods of high-velocities are seperated by longer periods without "
                           "movement."
                           "\n\n"
                           "\\item ``none'': No rate or state dependence of the friction angle is applied. "
                           "\n\n"
                           "\\item ``dynamic friction'': The friction angle is rate dependent."
                           "When dynamic angles of friction are specified, "
                           "the friction angle will be weakened for high strain rates with: "
                           "$\\mu = \\mu_d + \\frac(\\mu_s-\\mu_d)(1+(\\frac(\\dot{\\epsilon}_{ii})(\\dot{\\epsilon}_C)))^x$  "
                           "where $\mu_s$ and $\mu_d$ are the friction angle at low and high strain rates, "
                           "respectively. $\\dot{\\epsilon}_{ii}$ is the second invariant of the strain rate and "
                           "$\\dot{\\epsilon}_C$ is the characterisitc strain rate where $\\mu = (\\mu_s+\\mu_d)/2$. "
                           "x controls how smooth or step-like the change from $\mu_s$ to $\\mu_d$ is. "
                           "The equation is modified after Equation (13) in \\cite{van_dinther_seismic_2013}. "
                           "\n\n"
                           "\\item ``rate and state dependent friction'': A state variable theta is introduced "
                           "and the friction angle is calculated using classic aging rate-and-state friction by "
                           "Ruina (1983) as described by Equations (4--7) in \\cite{sobolev_modeling_2017}:\n"
                           "$\\mu = \\mu_{st} + a \\cdot ln\\big( \\frac{V}{V_{st}} \\big) + b \\cdot ln\\big( \\frac{\\Theta V_{st}}{L} \\big)$,\n"
                           "$\\frac{d\\Theta}{dt} = 1-\\frac{\\Theta V}{L} $.\n"
                           "Assuming that velocities are constant at any time step, this can be analytically integrated: \n"
                           "$\\Theta_{n+1} = \\frac{L}{V_{n+1}} + \big(\\Theta_n - \\frac{L}{V_{n+1}}\\big)*exp\\big(-\\frac{V_{n+1}\\Delta t}{L}\\big)$.\n"
                           "Pore fluid pressure can be taken into account by specifying the 'Effective friction "
                           "factor', which uses $\\mu* = \\mu\\big(1-\\frac{P_f}{\\sigma_n} \\big)$. "
                           "Reasonable values for a and b are 0.01 and 0.015, respectively, see \\cite{sobolev_modeling_2017}. "
                           "In ASPECT the state variable is confined to positive values: if it becomes $<=0$ during the computation "
                           "it is set to 1e-10. The friction angle is set to 0 if negative. "
                           "\n\n"
                           "\\item ``rate and state dependent friction plus linear slip weakening'': ToDo: all, this is an empty model atm. "
                           "Method taken from \\cite{sobolev_modeling_2017}. The friction coefficient is computed as "
                           "$\\mu = \\mu_{st} + a \\cdot ln\\big( \\frac{V}{V_{st}} \\big) + b \\cdot ln\\big( \\frac{\\Theta V_{st}}{L} \\big) - \\Delta \\mu(D)$"
                           "where D is the slip at point in fault at the first timestep of earthquake. "
                           "\n\n"
                           "\\item ``slip rate dependent rate and state dependent friction'': The rate and state "
                           "parametera and the critical slip distance L are made slip rate dependent. The friction "
                           "coefficient is computed as in 'rate and state dependent friction'. But a and L are not "
                           "constant, but are computed as follws, see \\citep{Im_im_slip-rate-dependent_2020} for details."
                           "$a(V) = a_0 + s_a log_{10}\\left(\\frac{V_a+V}{V_a}\\right)$ and "
                           "$L(V) = L_0 + s_L log_{10}\\left(\\frac{V_L+V}{V_L}\\right)$."
                           "So a and L have a log linear dependence on velocity with slopes od $s_a$ and $s_L$. "
                           "Parameters in their paper have the following values: "
                           "$L_0=10\\mu m$, $s_L=60\\mu m$, $V_L=100\\mu m/s$ and "
                           "$a_0=0.005$, $s_a=0.0003$, $V_a=100\\mu m/s$. "
                           "In ASPECT the initial values $a_0$ and $L_0$ are the rate and state friction parameters "
                           "indicated in 'Critical slip distance' and 'Rate and state parameter a function'."
                           "\n\n"
                           "\\item ``regularized rate and state friction'': The friction coefficient is computed using: "
                           "$\\mu = a\\cdot sinh^{-1}\\left[\\frac{V}{2V_0}exp\\left(\\frac{\\mu_0+b\cdot ln(V_0\theta/L)}{a}\\right)\\right]$ "
                           "This is a high velocity approxiamtion and regularized version of the classic rate and state friction. "
                           "This formulation overcomes the problem of ill-posedness and the possibility of negative friction for "
                           "$V<<V_0$. It is for example used in \\cite{herrendorfer_invariant_2018}. ");

        // Dynamic friction paramters
        prm.declare_entry ("Dynamic characteristic strain rate", "1e-12",
                           Patterns::Double (0),
                           "The characteristic strain rate value, where the angle of friction takes the middle "
                           "between the dynamic and the static angle of friction. When the effective strain rate "
                           "in a cell is very high, the dynamic angle of friction is taken, when it is very low "
                           "the static angle of internal friction is chosen. Around the dynamic characteristic "
                           "strain rate, there is a smooth gradient from the static to the dynamic friction "
                           "angle. "
                           "Units: \\si{\\per\\second}.");

        prm.declare_entry ("Dynamic angles of internal friction", "9999",
                           Patterns::List(Patterns::Double(0)),
                           "List of dynamic angles of internal friction, $\\phi$, for background material and compositional "
                           "fields, for a total of N+1 values, where N is the number of compositional fields. "
                           "Dynamic angles of friction are used as the current friction angle when the effective "
                           "strain rate in a cell is well above the characteristic strain rate. If not specified, "
                           "the internal angles of friction are taken. "
                           "Units: \\si{\\degree}.");

        prm.declare_entry ("Dynamic friction smoothness exponent", "1",
                           Patterns::Double (0),
                           "An exponential factor in the equation for the calculation of the friction angle "
                           "when a static and a dynamic friction angle are specified. A factor of 1 returns the equation "
                           "to Equation (13) in \\cite{van_dinther_seismic_2013}. A factor between 0 and 1 makes the "
                           "curve of the friction angle vs. the strain rate more smooth, while a factor $>$ 1 makes "
                           "the change between static and dynamic friction angle more steplike. "
                           "Units: none.");

        prm.enter_subsection("Rate and state parameter a function");
        {
          /**
           * The function can be declared in dependence of depth,
           * cartesian coordinates or spherical coordinates. Note that the order
           * of spherical coordinates is r,phi,theta and not r,theta,phi, since
           * this allows for dimension independent expressions.
           */
          prm.declare_entry ("Coordinate system", "cartesian",
                             Patterns::Selection ("cartesian|spherical|depth"),
                             "A selection that determines the assumed coordinate "
                             "system for the function variables. Allowed values "
                             "are `cartesian', `spherical', and `depth'. `spherical' coordinates "
                             "are interpreted as r,phi or r,phi,theta in 2D/3D "
                             "respectively with theta being the polar angle. `depth' "
                             "will create a function, in which only the first "
                             "parameter is non-zero, which is interpreted to "
                             "be the depth of the point.");

          Functions::ParsedFunction<dim>::declare_parameters(prm,1);
        }
        prm.leave_subsection();
        prm.enter_subsection("Rate and state parameter b function");
        {
          /**
           * The function can be declared in dependence of depth,
           * cartesian coordinates or spherical coordinates. Note that the order
           * of spherical coordinates is r,phi,theta and not r,theta,phi, since
           * this allows for dimension independent expressions.
           */
          prm.declare_entry ("Coordinate system", "cartesian",
                             Patterns::Selection ("cartesian|spherical|depth"),
                             "A selection that determines the assumed coordinate "
                             "system for the function variables. Allowed values "
                             "are `cartesian', `spherical', and `depth'. `spherical' coordinates "
                             "are interpreted as r,phi or r,phi,theta in 2D/3D "
                             "respectively with theta being the polar angle. `depth' "
                             "will create a function, in which only the first "
                             "parameter is non-zero, which is interpreted to "
                             "be the depth of the point.");

          Functions::ParsedFunction<dim>::declare_parameters(prm,1);
        }
        prm.leave_subsection();

        prm.declare_entry ("Effective friction factor", "1",
                           Patterns::List(Patterns::Double(0)),
                           "A number that is multiplied with the coefficient of friction to take into "
                           "account the influence of pore fluid pressure. This makes the friction "
                           "coefficient an effective friction coefficient as in \\cite{sobolev_modeling_2017}. "
                           "Units: none.");

        prm.declare_entry ("Critical slip distance", "0.01",
                           Patterns::List(Patterns::Double(0)),
                           "The critical slip distance in rate and state friction. It is used to calculate the "
                           "state variable theta using the aging law: $\\frac{d\\Theta}{dt}=1-\\frac{\\Theta V}{L}$. "
                           "At steady state: $\\Theta = \\frac{L}{V} $. The critical slip distance "
                           "is often interpreted as the sliding distance required to renew "
                           "the contact population as it determines the critical nucleation size with: "
                           "$h*=\\frac{2}{\\pi}\\frac{\\mu b L}{(b-a)^2 \\sigma_n}, where L is the critical slip "
                           "distance, a and b are parameters to describe the rate and state dependence, $\\mu$ "
                           "is the friction coefficient, and $\\sigma_n$ is the normal stress on the fault. "
                           "Laboratory values of the critical slip distance are on the "
                           "order of microns. For geodynamic modelling \\cite{sobolev_modeling_2017} set this parameter "
                           "to 1--10 cm. In the SEAS benchmark \\citep{erickson_community_2020} they use 4 and 8 mm. "
                           "Units: \\si{\\meter}.");

        prm.declare_entry ("Quasi static strain rate", "1e-14",
                           Patterns::Double (0),
                           "The quasi static or reference strain rate used in rate and state friction. It is an "
                           "arbitrary strain rate at which friction equals the reference friction angle. "
                           "This happens when slip rate (which is represented in ASPECT as strain rate) "
                           "enters a steady state. Friction at this steady state is defined as: "
                           "$\\mu = \\mu_{st} = \\mu_0 + (a-b)ln\\big( \\frac{V}{V_0} \\big). "
                           "It should not be confused with the characteristic strain rate in dynamic friction. "
                           "Units: \\si{\\per\\second}.");

        prm.declare_entry ("Use radiation damping", "false",
                           Patterns::Bool (),
                           "Whether to include radiation damping or not. Radiation damping adds the term "
                           "$-\\etaV = \\frac{\\mu}{2c_s}$ to the yield stress, $\\mu$ is the elastic shear "
                           "modulus, $c_s$ the shear wave speed and V the slip rate \\citep{rice_spatio-temporal_1993}. "
                           "Radiation damping prevents velocities to increase to infinity at the small time steps of "
                           "earthquakes. It therewith assures that the governing equations continue to have a solution "
                           "during earthquakelike episodes. Unlike an inertial term it cannot be used to model rupture "
                           "propagation as it approximates seismic waves as energy outflow only. ");
        /* TODO:
        prm.declare_entry ("Friction state variable law", "aging law",
                          Patterns::Selection ("aging law|slip law"),
                          "A selection that determines the law used for the evolution of the state variable "
                          "$\\theta$ in rate-and-state friction. When the aging law (also called Dietrich law "
                          "or slowness law) is chosen, the state variable is computed with "
                          "$\\dot{\\theta}=1-\\frac{V\\theta}{D_c}$ while the slip law (or Ruina law) follows "
                          "$\\dot{\\theta}=-\\frac{V\\theta}{D_c}ln\\frac{V\\theta}{D_c}$, where $D_c$ is the "
                          "critical slip distance and V is velocity. The aging law indicates that state "
                          "increases when $V=0$, whereas the slip law requires slip to occur: no evolution in "
                          "state occurs unless $V\\neq0$ \\citep[read more in ]{scholz_mechanics_2019}."); */

        // Parameters for slip rate dependent rate and state friction
        prm.declare_entry ("Reference velocity for rate and state parameter a", "0.005",
                           Patterns::Double (0),
                           "The reference velocity used to modify the initial value for rate and state "
                           "parameter a in case of slip rate dependence. "
                           "Units: \\si{\\meter\\per\\second}.");

        prm.declare_entry ("Reference velocity for critical slip distance L", "0.01",
                           Patterns::Double (0),
                           "The reference velocity used to modify the initial value for rate and state "
                           "parameter L, the critical slip distance in case of slip rate dependence. "
                           "Units: \\si{\\meter\\per\\second}.");

        prm.declare_entry ("Slope of log dependence for rate and state parameter a", "3e-4",
                           Patterns::Double (0),
                           "Slope for the log linear slip rate dependence of rate and state parameter a."
                           "Units: \\si{\\meter}.");

        prm.declare_entry ("Slope of log dependence for critical slip distance L", "6e-5",
                           Patterns::Double (0),
                           "Slope for the log linear slip rate dependence of the critical slip distance. "
                           "Units: \\si{\\meter}.");
      }



      template <int dim>
      void
      FrictionOptions<dim>::parse_parameters (ParameterHandler &prm)
      {
        // Get the number of fields for composition-dependent material properties
        const unsigned int n_fields = this->n_compositional_fields() + 1;

        // Friction dependence parameters
        if (prm.get ("Friction dependence mechanism") == "none")
          friction_dependence_mechanism = independent;
        else if (prm.get ("Friction dependence mechanism") == "dynamic friction")
          friction_dependence_mechanism = dynamic_friction;
        else if (prm.get ("Friction dependence mechanism") == "rate and state dependent friction")
          friction_dependence_mechanism = rate_and_state_dependent_friction;
        else if (prm.get ("Friction dependence mechanism") == "rate and state dependent friction plus linear slip weakening")
          friction_dependence_mechanism = rate_and_state_dependent_friction_plus_linear_slip_weakening;
        else if (prm.get ("Friction dependence mechanism") == "slip rate dependent rate and state dependent friction")
          friction_dependence_mechanism = slip_rate_dependent_rate_and_state_dependent_friction;
        else if (prm.get ("Friction dependence mechanism") == "regularized rate and state friction")
          friction_dependence_mechanism = regularized_rate_and_state_friction;
        else
          AssertThrow(false, ExcMessage("Not a valid friction dependence option!"));

        if ((friction_dependence_mechanism == slip_rate_dependent_rate_and_state_dependent_friction)
            |(friction_dependence_mechanism == regularized_rate_and_state_friction)
            |(friction_dependence_mechanism == rate_and_state_dependent_friction_plus_linear_slip_weakening)
            |( friction_dependence_mechanism == rate_and_state_dependent_friction))
          {
            // Currently, it only makes sense to use a state variable when the nonlinear solver
            // scheme does a single Advection iteration and at minimum one Stokes iteration, as
            // the state variable is implemented as a material field. More
            // than one nonlinear Advection iteration will produce an unrealistic values
            // for the state variable.
            AssertThrow((this->get_parameters().nonlinear_solver ==
                         Parameters<dim>::NonlinearSolver::single_Advection_single_Stokes
                         ||
                         this->get_parameters().nonlinear_solver ==
                         Parameters<dim>::NonlinearSolver::single_Advection_iterated_Stokes
                         ||
                         this->get_parameters().nonlinear_solver ==
                         Parameters<dim>::NonlinearSolver::single_Advection_iterated_Newton_Stokes
                         ||
                         this->get_parameters().nonlinear_solver ==
                         Parameters<dim>::NonlinearSolver::single_Advection_iterated_defect_correction_Stokes),
                        ExcMessage("The rate and state friction will only work with the nonlinear "
                                   "solver schemes 'single Advection, single Stokes' and "
                                   "'single Advection, iterated Stokes', 'single Advection, "
                                   "iterated defect correction Stokes', 'ginle advection, iterated Newton Stokes'"));
          }

        // Dynamic friction parameters
        dynamic_characteristic_strain_rate = prm.get_double("Dynamic characteristic strain rate");

        if (prm.get ("Dynamic angles of internal friction") == "9999")
          {
            // If not specified, the internal angles of friction are used, so there is no dynamic friction in the model
            dynamic_angles_of_internal_friction = drucker_prager_parameters.angles_internal_friction;
          }
        else
          {
            dynamic_angles_of_internal_friction = Utilities::possibly_extend_from_1_to_N (Utilities::string_to_double(Utilities::split_string_list(prm.get("Dynamic angles of internal friction"))),
                                                                                          n_fields,
                                                                                          "Dynamic angles of internal friction");

            // Convert angles from degrees to radians
            for (unsigned int i = 0; i<dynamic_angles_of_internal_friction.size(); ++i)
              {
                if (dynamic_angles_of_internal_friction[i] > 90)
                  {
                    AssertThrow(false, ExcMessage("Dynamic angles of friction must be <= 90 degrees"));
                  }
                else
                  {
                    dynamic_angles_of_internal_friction[i] *= numbers::PI/180.0;
                  }
              }
          }

        dynamic_friction_smoothness_exponent = prm.get_double("Dynamic friction smoothness exponent");

        // Rate and state friction parameters
        // Todo: add other RSF options! Or take get_use_theta()
        if (friction_dependence_mechanism == rate_and_state_dependent_friction)
          {
            AssertThrow(this->introspection().compositional_name_exists("theta"),
                        ExcMessage("Material model with rate-and-state friction only works "
                                   "if there is a compositional field that is called theta. It is "
                                   "used to store the state variable."));
          }

        effective_friction_factor = Utilities::possibly_extend_from_1_to_N (Utilities::string_to_double(Utilities::split_string_list(prm.get("Effective friction factor"))),
                                                                            n_fields,
                                                                            "Effective friction factor");

        critical_slip_distance = prm.get_double("Critical slip distance");

        quasi_static_strain_rate = prm.get_double("Quasi static strain rate");

        use_radiation_damping = prm.get_bool("Use radiation damping");

        prm.enter_subsection("Rate and state parameter a function");
        {
          coordinate_system_a = Utilities::Coordinates::string_to_coordinate_system(prm.get("Coordinate system"));
        }
        try
          {
            rate_and_state_parameter_a_function
              = std_cxx14::make_unique<Functions::ParsedFunction<dim>>(n_fields);
            rate_and_state_parameter_a_function->parse_parameters (prm);
          }
        catch (...)
          {
            std::cerr << "FunctionParser failed to parse\n"
                      << "\t a function\n"
                      << "with expression \n"
                      << "\t' " << prm.get("Function expression") << "'";
            throw;
          }
        prm.leave_subsection();


        prm.enter_subsection("Rate and state parameter b function");
        {
          coordinate_system_b = Utilities::Coordinates::string_to_coordinate_system(prm.get("Coordinate system"));
        }
        try
          {
            rate_and_state_parameter_b_function
              = std_cxx14::make_unique<Functions::ParsedFunction<dim>>(n_fields);
            rate_and_state_parameter_b_function->parse_parameters (prm);
          }
        catch (...)
          {
            std::cerr << "FunctionParser failed to parse\n"
                      << "\t a function\n"
                      << "with expression \n"
                      << "\t' " << prm.get("Function expression") << "'";
            throw;
          }
        prm.leave_subsection();

        // parameters for slip rate dependent rate and state friction

        ref_v_for_a = prm.get_double("Reference velocity for rate and state parameter a");

        ref_v_for_L = prm.get_double("Reference velocity for critical slip distance L");

        slope_s_for_a = prm.get_double("Slope of log dependence for rate and state parameter a");

        slope_s_for_L = prm.get_double("Slope of log dependence for critical slip distance L");

      }
    }
  }
}



// Explicit instantiations
namespace aspect
{
  namespace MaterialModel
  {
#define INSTANTIATE(dim) \
  namespace Rheology \
  { \
    template class FrictionOptions<dim>; \
  }

    ASPECT_INSTANTIATE(INSTANTIATE)

#undef INSTANTIATE
  }
}

