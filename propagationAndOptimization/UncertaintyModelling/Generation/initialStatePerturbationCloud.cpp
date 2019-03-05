/*    Copyright (c) 2010-2018, Delft University of Technology
 *    All rigths reserved
 *
 *    This file is part of the Tudat. Redistribution and use in source and
 *    binary forms, with or without modification, are permitted exclusively
 *    under the terms of the Modified BSD license. You should have received
 *    a copy of the license with this file. If not, please or visit:
 *    http://tudat.tudelft.nl/LICENSE.
 */

#include <Tudat/SimulationSetup/tudatEstimationHeader.h>

#include "propagationAndOptimization/applicationOutput.h"

int main( )
{
    std::string outputPath = tudat_applications::getOutputPath( "UncertaintyModelling/" );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////            USING STATEMENTS              //////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    using namespace tudat;
    using namespace tudat::simulation_setup;
    using namespace tudat::propagators;
    using namespace tudat::numerical_integrators;
    using namespace tudat::orbital_element_conversions;
    using namespace tudat::basic_mathematics;
    using namespace tudat::gravitation;
    using namespace tudat::numerical_integrators;
    using namespace tudat::estimatable_parameters;
    using namespace tudat::ephemerides;
    using namespace tudat::statistics;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////     CREATE ENVIRONMENT AND VEHICLE       //////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Load Spice kernels.
    spice_interface::loadStandardSpiceKernels( );

    for( int runCase = 0; runCase < 4; runCase++ )
    {
        // Set simulation time settings.
        double simulationStartEpoch = 0.0;
        double simulationEndEpoch;
        if( runCase == 0 || runCase == 1 )
        {
            simulationEndEpoch = 30.0 * tudat::physical_constants::JULIAN_DAY;
        }
        else if( runCase == 2 || runCase == 3 )
        {
            simulationEndEpoch = 3.0 * tudat::physical_constants::JULIAN_DAY;
        }

        // Define body settings for simulation.
        std::vector< std::string > bodiesToCreate;
        bodiesToCreate.push_back( "Sun" );
        bodiesToCreate.push_back( "Earth" );
        bodiesToCreate.push_back( "Moon" );
        bodiesToCreate.push_back( "Mars" );
        bodiesToCreate.push_back( "Venus" );

        // Create body objects.
        std::map< std::string, std::shared_ptr< BodySettings > > bodySettings =
                getDefaultBodySettings( bodiesToCreate, simulationStartEpoch - 300.0, simulationEndEpoch + 300.0 );
        for( unsigned int i = 0; i < bodiesToCreate.size( ); i++ )
        {
            bodySettings[ bodiesToCreate.at( i ) ]->ephemerisSettings->resetFrameOrientation( "J2000" );
            bodySettings[ bodiesToCreate.at( i ) ]->rotationModelSettings->resetOriginalFrame( "J2000" );
        }
        NamedBodyMap bodyMap = createBodies( bodySettings );

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////////             CREATE VEHICLE            /////////////////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Create spacecraft object.
        bodyMap[ "Asterix" ] = std::make_shared< simulation_setup::Body >( );
        bodyMap[ "Asterix" ]->setConstantBodyMass( 400.0 );

        bodyMap[ "Asterix" ]->setEphemeris( std::make_shared< TabulatedCartesianEphemeris< > >(
                                                std::shared_ptr< interpolators::OneDimensionalInterpolator
                                                < double, Eigen::Vector6d > >( ), "Earth", "J2000" ) );

        // Create aerodynamic coefficient interface settings.
        double referenceArea = 4.0;
        double aerodynamicCoefficient = 1.2;
        std::shared_ptr< AerodynamicCoefficientSettings > aerodynamicCoefficientSettings =
                std::make_shared< ConstantAerodynamicCoefficientSettings >(
                    referenceArea, aerodynamicCoefficient * Eigen::Vector3d::UnitX( ), 1, 1 );

        // Create and set aerodynamic coefficients object
        bodyMap[ "Asterix" ]->setAerodynamicCoefficientInterface(
                    createAerodynamicCoefficientInterface( aerodynamicCoefficientSettings, "Asterix" ) );

        // Create radiation pressure settings
        double referenceAreaRadiation = 4.0;
        double radiationPressureCoefficient = 1.2;
        std::vector< std::string > occultingBodies;
        occultingBodies.push_back( "Earth" );
        std::shared_ptr< RadiationPressureInterfaceSettings > asterixRadiationPressureSettings =
                std::make_shared< CannonBallRadiationPressureInterfaceSettings >(
                    "Sun", referenceAreaRadiation, radiationPressureCoefficient, occultingBodies );

        // Create and set radiation pressure settings
        bodyMap[ "Asterix" ]->setRadiationPressureInterface(
                    "Sun", createRadiationPressureInterface(
                        asterixRadiationPressureSettings, "Asterix", bodyMap ) );


        // Finalize body creation.
        setGlobalFrameBodyEphemerides( bodyMap, "SSB", "J2000" );

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////////            CREATE ACCELERATIONS          //////////////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Define propagator settings variables.
        SelectedAccelerationMap accelerationMap;
        std::vector< std::string > bodiesToPropagate;
        std::vector< std::string > centralBodies;

        // Define propagation settings.
        std::map< std::string, std::vector< std::shared_ptr< AccelerationSettings > > > accelerationsOfAsterix;
        accelerationsOfAsterix[ "Earth" ].push_back( std::make_shared< SphericalHarmonicAccelerationSettings >( 4, 4 ) );

        accelerationsOfAsterix[ "Sun" ].push_back( std::make_shared< AccelerationSettings >(
                                                       basic_astrodynamics::central_gravity ) );
        accelerationsOfAsterix[ "Moon" ].push_back( std::make_shared< AccelerationSettings >(
                                                        basic_astrodynamics::central_gravity ) );
        accelerationsOfAsterix[ "Mars" ].push_back( std::make_shared< AccelerationSettings >(
                                                        basic_astrodynamics::central_gravity ) );
        accelerationsOfAsterix[ "Venus" ].push_back( std::make_shared< AccelerationSettings >(
                                                         basic_astrodynamics::central_gravity ) );
        accelerationsOfAsterix[ "Sun" ].push_back( std::make_shared< AccelerationSettings >(
                                                       basic_astrodynamics::cannon_ball_radiation_pressure ) );
        accelerationsOfAsterix[ "Earth" ].push_back( std::make_shared< AccelerationSettings >(
                                                         basic_astrodynamics::aerodynamic ) );

        accelerationMap[ "Asterix" ] = accelerationsOfAsterix;
        bodiesToPropagate.push_back( "Asterix" );
        centralBodies.push_back( "Earth" );

        basic_astrodynamics::AccelerationMap accelerationModelMap = createAccelerationModelsMap(
                    bodyMap, accelerationMap, bodiesToPropagate, centralBodies );

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////////             CREATE PROPAGATION SETTINGS            ////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Set Keplerian elements for Asterix.
        Eigen::Vector6d asterixInitialStateInKeplerianElements;
        asterixInitialStateInKeplerianElements( semiMajorAxisIndex ) = 7500.0E3;
        asterixInitialStateInKeplerianElements( eccentricityIndex ) = 0.1;
        asterixInitialStateInKeplerianElements( inclinationIndex ) = unit_conversions::convertDegreesToRadians( 85.3 );
        asterixInitialStateInKeplerianElements( argumentOfPeriapsisIndex ) = unit_conversions::convertDegreesToRadians( 235.7 );
        asterixInitialStateInKeplerianElements( longitudeOfAscendingNodeIndex ) = unit_conversions::convertDegreesToRadians( 23.4 );
        asterixInitialStateInKeplerianElements( trueAnomalyIndex ) = unit_conversions::convertDegreesToRadians( 139.87 );

        double earthGravitationalParameter = bodyMap.at( "Earth" )->getGravityFieldModel( )->getGravitationalParameter( );
        const Eigen::Vector6d asterixInitialState = convertKeplerianToCartesianElements(
                    asterixInitialStateInKeplerianElements, earthGravitationalParameter );

        std::shared_ptr< TranslationalStatePropagatorSettings< double > > propagatorSettings =
                std::make_shared< TranslationalStatePropagatorSettings< double > >(
                    centralBodies, accelerationModelMap, bodiesToPropagate, asterixInitialState, simulationEndEpoch );

        const double fixedStepSize = 15.0;
        std::shared_ptr< IntegratorSettings< > > integratorSettings =
                std::make_shared< IntegratorSettings< > >( rungeKutta4, 0.0, fixedStepSize );

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////////    DEFINE PARAMETERS FOR WHICH SENSITIVITY IS TO BE COMPUTED   ////////////////////////////////
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Define list of parameters to estimate.
        std::vector< std::shared_ptr< EstimatableParameterSettings > > parameterNames;
        parameterNames.push_back( std::make_shared< InitialTranslationalStateEstimatableParameterSettings< double > >(
                                      "Asterix", asterixInitialState, "Earth" ) );
        parameterNames.push_back( std::make_shared< EstimatableParameterSettings >( "Asterix", radiation_pressure_coefficient ) );
        parameterNames.push_back( std::make_shared< EstimatableParameterSettings >( "Asterix", constant_drag_coefficient ) );
        parameterNames.push_back( std::make_shared< SphericalHarmonicEstimatableParameterSettings >(
                                      2, 0, 2, 2, "Earth", spherical_harmonics_cosine_coefficient_block ) );
        parameterNames.push_back( std::make_shared< SphericalHarmonicEstimatableParameterSettings >(
                                      2, 1, 2, 2, "Earth", spherical_harmonics_sine_coefficient_block ) );

        // Create parameters
        std::shared_ptr< estimatable_parameters::EstimatableParameterSet< double > > parametersToEstimate =
                createParametersToEstimate( parameterNames, bodyMap );

        // Print identifiers and indices of parameters to terminal.
        printEstimatableParameterEntries( parametersToEstimate );

        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        ///////////////////////             PROPAGATE ORBIT AND VARIATIONAL EQUATIONS         /////////////////////////////////
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Create simulation object and propagate dynamics.
        SingleArcVariationalEquationsSolver< > variationalEquationsSimulator(
                    bodyMap, integratorSettings, propagatorSettings, parametersToEstimate, true,
                    std::shared_ptr< numerical_integrators::IntegratorSettings< double > >( ), false, true );

        std::map< double, Eigen::MatrixXd > stateTransitionResult =
                variationalEquationsSimulator.getNumericalVariationalEquationsSolution( ).at( 0 );
        std::map< double, Eigen::MatrixXd > sensitivityResult =
                variationalEquationsSimulator.getNumericalVariationalEquationsSolution( ).at( 1 );
        std::map< double, Eigen::VectorXd > integrationResult =
                variationalEquationsSimulator.getDynamicsSimulator( )->getEquationsOfMotionNumericalSolution( );

        input_output::writeDataMapToTextFile(
                    integrationResult, "monteCarloNominalResult_" + std::to_string( runCase ) + "_" +
                                               + ".dat", outputPath );

        double errorMagnitude;
        if( runCase == 0 || runCase == 2 )
        {
            errorMagnitude = 1.0E3;
        }
        else if( runCase == 1 || runCase == 3 )
        {
            errorMagnitude = 1.0;
        }
        std::function< double( ) > initialPositionErrorFunction = createBoostContinuousRandomVariableGeneratorFunction(
                    normal_boost_distribution, { 0.0, errorMagnitude }, 0.0 );

        Eigen::MatrixXd outputMap = Eigen::MatrixXd( 6, 4 );
        for( unsigned int i = 0; i < 100; i++ )
        {
            std::cout<<"RUN: "<<i<<std::endl;
            Eigen::Vector3d currentInitialPositionError;
            currentInitialPositionError( 0 ) = initialPositionErrorFunction( );
            currentInitialPositionError( 1 ) = initialPositionErrorFunction( );
            currentInitialPositionError( 2 ) = initialPositionErrorFunction( );

            Eigen::VectorXd perturbedInitialState = asterixInitialState;
            perturbedInitialState.segment( 0, 3 ) += currentInitialPositionError;

            std::shared_ptr< TranslationalStatePropagatorSettings< double > > perturbedPropagatorSettings =
                    std::make_shared< TranslationalStatePropagatorSettings< double > >(
                        centralBodies, accelerationModelMap, bodiesToPropagate, perturbedInitialState, simulationEndEpoch );

            SingleArcDynamicsSimulator< > perturbedDynamicsSimulator( bodyMap, integratorSettings, perturbedPropagatorSettings );
            std::map< double, Eigen::VectorXd > perturbedIntegrationResult =
                    perturbedDynamicsSimulator.getEquationsOfMotionNumericalSolution( );
            std::map< double, Eigen::VectorXd > linearizationError;
            for( auto stateIterator : perturbedIntegrationResult )
            {
                linearizationError[ stateIterator.first ] =
                    perturbedIntegrationResult[ stateIterator.first ] - integrationResult[ stateIterator.first ] -
                        stateTransitionResult[ stateIterator.first ].block( 0, 0, 6, 3 ) * currentInitialPositionError;
            }

            outputMap.block( 0, 0, 3, 1 ) = currentInitialPositionError;
            outputMap.block( 0, 1, 6, 1 ) = stateTransitionResult.rbegin( )->second.block( 0, 0, 6, 3 ) * currentInitialPositionError;
            outputMap.block( 0, 2, 6, 1 ) = perturbedIntegrationResult.rbegin( )->second - integrationResult.rbegin( )->second;
            outputMap.block( 0, 3, 6, 1 ) = perturbedIntegrationResult.rbegin( )->second;

            input_output::writeMatrixToFile(
                        outputMap, "monteCarloInitialState_" +
                        std::to_string( runCase ) + "_" +
                        std::to_string( i ) + ".dat", 16, outputPath );

            //        std::cout<<( stateTransitionResult.rbegin( )->second.block( 0, 0, 6, 3 ) * currentInitialPositionError ).transpose( )<<std::endl;
            //        std::cout<<( perturbedIntegrationResult.rbegin( )->second - integrationResult.rbegin( )->second ).transpose( )<<std::endl;
            //        std::cout<<( stateTransitionResult.rbegin( )->second.block( 0, 0, 6, 3 ) * currentInitialPositionError ).transpose( )  -
            //                   ( perturbedIntegrationResult.rbegin( )->second - integrationResult.rbegin( )->second ).transpose( )<<std::endl<<std::endl;

            input_output::writeDataMapToTextFile(
                        linearizationError, "initialStateLinearizationError_" +
                        std::to_string( runCase ) + "_" +
                        std::to_string( i ) + ".dat", outputPath );
        }
    }
    //    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //    ///////////////////////        PROVIDE OUTPUT TO CONSOLE AND FILES           //////////////////////////////////////////
    //    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    //    std::string outputSubFolder = "PerturbedSatelliteVariationalExample/";

    //    // Write perturbed satellite propagation history to file.
    //    input_output::writeDataMapToTextFile( integrationResult,
    //                                          "singlePerturbedSatellitePropagationHistory.dat",
    //                                          tudat_applications::getOutputPath( ) + outputSubFolder,
    //                                          "",
    //                                          std::numeric_limits< double >::digits10,
    //                                          std::numeric_limits< double >::digits10,
    //                                          "," );

    //    input_output::writeDataMapToTextFile( stateTransitionResult,
    //                                          "singlePerturbedSatelliteStateTransitionHistory.dat",
    //                                          tudat_applications::getOutputPath( ) + outputSubFolder,
    //                                          "",
    //                                          std::numeric_limits< double >::digits10,
    //                                          std::numeric_limits< double >::digits10,
    //                                          "," );

    //    input_output::writeDataMapToTextFile( sensitivityResult,
    //                                          "singlePerturbedSatelliteSensitivityHistory.dat",
    //                                          tudat_applications::getOutputPath( ) + outputSubFolder,
    //                                          "",
    //                                          std::numeric_limits< double >::digits10,
    //                                          std::numeric_limits< double >::digits10,
    //                                          "," );


    // Final statement.
    // The exit code EXIT_SUCCESS indicates that the program was successfully executed.
    return EXIT_SUCCESS;
}

